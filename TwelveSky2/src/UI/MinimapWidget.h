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
//     MinimapWindowMode {Full, ClampedCenter, Free}.
//
// BEW-01 (2026-07-16) — LE FOND DE CARTE EST DÉSORMAIS RÉELLEMENT BLITTÉ. Deux affirmations
// de l'ancien bandeau étaient FAUSSES et sont corrigées ici (re-prouvées en IDA) :
//   (1) « échelles par mode (dword_14A906C/dword_14A9070) » : FAUX. @0x681560 `mov ecx,
//       ds:dword_14A906C[eax]` / @0x68157B `mov ecx, ds:dword_14A9070[eax]` avec eax = 0x28*mode.
//       Or dword_14A906C == unk_14A9068+4 et dword_14A9070 == unk_14A9068+8 : ce sont les champs
//       +4/+8 de l'OBJET TEXTURE d'index `mode` (stride 40), c.-à-d. la LARGEUR et la HAUTEUR
//       LOGIQUES de l'image de zone (`qmemcpy(this+1, header, 0x1C)` @0x6A2FFE de
//       Tex_LoadCompressedDDS 0x6A2E80). Aucune table d'échelle n'existe.
//   (2) « bornes monde NON chargées dans ce modèle » : FAUX depuis GX-ICON-01 —
//       WorldAssets::MinimapWorldBounds() (World/WorldIntegration.h) fournit exactement
//       dword_14A88C8 (@0x681513-0x68154B, avec ses DEUX fchs sur Z).
// Les deux arrivent maintenant par SetSourceProvider (voir MinimapSource ci-dessous), et la
// constante empirique kWorldViewRadius=4000 a DISPARU. Widget toujours SANS ressource GPU propre :
// il EMPRUNTE la texture du monde (dans la cible elle vit à world+2092+40*mode, pas dans le HUD).
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
#include <functional>
#include <utility>

#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Game/GameState.h"
#include "Core/Types.h"

namespace ts2::ui {

// Fond de carte de la zone courante, tel que le bloc mini-carte de UI_GameHud_Render le lit.
// Un seul fournisseur (le monde) le remplit ; le widget ne possède RIEN ici.
//   tex             <- unk_14A9068 + 0x28*mode, champ +36 (surface D3D9)          @0x681AAB/@0x6A3040
//   imgW / imgH     <- champs +4 / +8 du même objet = dims LOGIQUES de l'image    @0x681560/@0x68157B
//   minX / maxX     <- dword_14A88C8[+0] / [+0Ch]                                 @0x681519/@0x681527
//   negMaxZ/negMinZ <- -dword_14A88C8[+14h] / -dword_14A88C8[+8]  (fchs !)        @0x681535/@0x681546
struct MinimapSource {
    IDirect3DTexture9* tex = nullptr;
    int   imgW = 0, imgH = 0;
    float minX = 0.0f, maxX = 0.0f;
    float negMaxZ = 0.0f, negMinZ = 0.0f;
};

// Renvoie false si la zone n'a pas (encore) de fond de carte -> repli sur l'aplat.
// `mode` = this+616 (0/1/2) : c'est lui qui indexe les 3 textures dans la cible.
using MinimapSourceProvider = std::function<bool(int mode, MinimapSource& out)>;

// Rectangle écran simple (même forme que GameHud::HudRect, dupliqué ici pour ne
// pas coupler MinimapWidget.h à GameHud.h — voir bandeau ci-dessus).
struct MmRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Mode de fenêtre visible de la mini-carte (this+616 dans le binaire, §12b). Ce champ est AUSSI
// l'index des 3 textures de zone (`imul ecx, 28h ; add ecx, offset unk_14A9068` @0x681AA8) : les
// « modes » sont en fait 3 IMAGES de résolution décroissante (= niveaux de zoom), d'où les boutons
// −/+ 0x6773DE/0x67748E. Comportement de blit, re-prouvé à l'octet :
//   Full          (0) @0x6818FB : crop 145x128 défilant, srcX/srcY clampés aux bords de l'image.
//   ClampedCenter (1) @0x681ABB : IDENTIQUE au case 0 (doc §12b : « redondant/vestige de refactor »).
//   Free          (2) @0x681C7D : srcX=srcY=0 et UI_DrawSprite a4=0 -> rect source PLEIN
//                                 (0,0,imgW,imgH), image entière à sa taille naturelle ; et la
//                                 boucle PNJ est intégralement sautée (@0x681DF3).
enum class MinimapWindowMode : uint8_t { Full = 0, ClampedCenter = 1, Free = 2 };

class MinimapWidget {
public:
    MinimapWidget() = default;

    // Calcule le layout pour ces dimensions écran (cGameHud n'ayant pas de hook
    // de resize dynamique, appelé une fois depuis GameHud::Init — même
    // limitation que le reste du HUD).
    void Init(int screenW, int screenH);
    void OnScreenResize(int screenW, int screenH) { Init(screenW, screenH); }

    // BEW-01 — branche le fond de carte de la zone. SANS ce provider, DrawPanels retombe sur
    // l'aplat kViewportBg (le binaire, lui, ne dessine JAMAIS d'aplat : il blitte toujours
    // unk_14A9068[mode]). À poser depuis Scene/SceneManager.cpp (seul propriétaire de hud_ ET
    // worldAssets_) — cf. rapport de front pour la ligne exacte.
    void SetSourceProvider(MinimapSourceProvider p) { sourceProvider_ = std::move(p); }
    bool HasSourceProvider() const { return static_cast<bool>(sourceProvider_); }

    // Bascule petit/grand (bouton §12a). SetBigMode permet un câblage externe
    // (ex. touche raccourcie) en plus du hit-test interne d'OnMouseDown.
    void ToggleSize();
    void SetBigMode(bool big);
    bool BigMode() const { return bigMode_; }

    // Mode de fenêtre visible (§12b). Défaut : Full (=0), valeur écrite par UI_GameHud_Init
    // 0x675184 (`mov [ecx+268h], 0`). ATTENTION [0x6773DE zoom− / 0x67748E zoom+] : this+616
    // est en réalité un NIVEAU DE ZOOM borné [0,2] (2 boutons −/+ en grand mode :
    // this[0x268]-=1 / +=1), PAS un mode 3 états librement affectable. SetWindowMode() n'a donc
    // AUCUNE contrepartie binaire (setter arbitraire) ; conservé faute des 2 boutons de zoom.
    // NB : le motif « table d'échelle dword_14A906C/14A9070 non dumpée » invoqué ici auparavant
    // était FAUX (ce sont les champs +4/+8 de la texture = ses dims logiques — cf. bandeau de
    // tête, BEW-01) ; il ne reste donc à faire que les 2 boutons -> TODO hors périmètre.
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

    // --- Projection PROUVÉE (remplace l'ancien ProjectToViewport « radar » à rayon fixe) -----
    // Monde -> pixel DANS L'IMAGE de zone. @0x681915-0x68193C (X) / @0x681942-0x681969 (Y) :
    //   px = ftol( imgW * ( wx - minX)     / (maxX    - minX) )
    //   py = ftol( imgH * (-wz - negMaxZ)  / (negMinZ - negMaxZ) )
    // (le -wz vient du `fchs` @0x68190D ; Crt_ftol 0x760810 tronque vers zéro.)
    static void WorldToImagePixel(const MinimapSource& s, float wx, float wz, int& px, int& py);

    // Crop 145x128 défilant centré sur le self, clampé aux bords de l'image.
    // @0x68198F-0x6819FC (X) / @0x681A02-0x681A54 (Y) :
    //   srcX = clamp(selfPx - 0x48, 0, imgW - 0x91) ; srcY = clamp(selfPy - 0x40, 0, imgH - 0x80)
    static void ComputeCrop(const MinimapSource& s, int selfPx, int selfPy, int& srcX, int& srcY);

    // Pixel image -> pixel écran, en tenant compte du crop courant. @0x681E7E-0x681F05 :
    //   sx = frame.x + px - srcX + 4   ;   sy = frame.y + py - srcY + 0x2A
    // Renvoie false si HORS des bornes -> le marqueur est OMIS (jmp loc_68229E @0x681F05),
    // JAMAIS clampé au bord : l'ancien modèle « radar » (clamp en Full/ClampedCenter, omission
    // en Free) était inversé et faux dans les deux sens.
    //   gardé ssi  frame.x+4 <= sx <= frame.x+0x96   ET   frame.y+0x2A <= sy <= frame.y+0xA8
    bool MarkerScreenPos(int px, int py, int srcX, int srcY, int& sx, int& sy) const;

    MinimapSourceProvider sourceProvider_; // BEW-01 — fond de carte fourni par le monde

    int  screenW_ = ts2::kRefWidth;
    int  screenH_ = ts2::kRefHeight;
    // Défauts corrigés d'après UI_GameHud_Init 0x675140 (recoupé en IDA) :
    //   this+612 (0x264) <- 1 @0x675177 : la mini-carte démarre en GRAND mode (défaut false
    //     précédent = infidèle).
    //   this+616 (0x268) <- 0 @0x675184 : niveau this+616 = 0 (voir bandeau WindowMode ci-dessus).
    bool bigMode_ = true;                                               // this+612 (défaut 1 @0x675177)
    MinimapWindowMode windowMode_ = MinimapWindowMode::Full;            // this+616 (défaut 0 @0x675184)

    game::EntityId questHighlightMonster_{}; // mécanisme de clignotement (inerte)

    Layout layout_{};
};

} // namespace ts2::ui

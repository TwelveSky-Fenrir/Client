// UI/PanelSkin.h — fond de panneau texturé pour les fenêtres UI (ts2::ui), avec
// repli automatique sur UiContext::FillRect si la texture est indisponible. Ne
// modifie NI Dialog NI UiContext (UI/UIManager.h) : utilitaire additif partagé par
// les 11 fenêtres à fond de panneau, pour éviter de dupliquer 11 fois la même
// logique charge-une-fois + repli.
//
// -----------------------------------------------------------------------------
// PROVENANCE DES TEXTURES — MÉTHODE PROUVÉE PAR DÉSASSEMBLAGE (révision W9).
// -----------------------------------------------------------------------------
// L'ancien bandeau de ce fichier affirmait qu'« aucun outil MCP idaTs2 n'était
// disponible » et que le mapping fichier -> fenêtre résultait d'une « analyse
// STATISTIQUE » des tailles de .IMG. Cette justification est PÉRIMÉE : la chaîne
// slot -> fichier est désormais établie instruction par instruction, et les
// indices littéraux qu'elle servait à excuser sont FAUX. Vérité terrain :
//
//   1. AssetMgr_UpdateUnloadExpired 0x4E2050 : `imul ecx, 94h` (=148, stride d'un
//      Sprite2D) puis `lea ecx, [edx+ecx+20h]` avec edx = g_ModelMotionArray
//      0x8E8B30  =>  base du pool d'atlas UI = 0x8E8B30 + 0x20 = 0x8E8B50
//      (symbole IDB `g_AssetMgr_UiAtlasSlots`), 0x1194 = 4500 slots, catégorie 1.
//   2. Sprite2D_BuildPath 0x4D6900 case 1 (@0x4d6913) :
//        g_UseTRVariant (0x1669190) == 1 -> "G03_GDATA\D01_GIMAGE2D\001\TR\001_%05d.IMG" (@0x4d6928)
//        sinon                           -> "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"    (@0x4d6945)
//      avec %05d = (index de slot) + 1.
//
//   => slot = (adresse_unk - 0x8E8B50) / 148, la division DEVANT tomber juste
//      (reste 0) ; le numéro de fichier vaut slot + 1.
//
// Slots RÉELLEMENT prouvés par xref (à substituer aux devinettes ; le câblage
// vit dans les .cpp des fenêtres, cf. le TODO de câblage plus bas) :
//
//   unk_8F7608 -> (0x8F7608-0x8E8B50)/148 = 406,  reste 0 -> 001_00407.IMG
//       GABARIT GÉNÉRIQUE, PAS « le fond GUILDE » : xrefs_to 0x8F7608 montre qu'il
//       est PARTAGÉ par UI_ClanWin_Draw 0x5DA210 (@0x5da28c) ET UI_NpcMenu_Draw
//       0x5DFC30 (@0x5dfdb1) (+ leurs OnLDown/OnLUp). L'étiqueter par fenêtre est
//       faux : plusieurs dialogues réutilisent le même gabarit.
//   unk_94B87C -> (0x94B87C-0x8E8B50)/148 = 2735, reste 0 -> 001_02736.IMG
//       Panneau boutique/gemmes : UI_Shop_Render 0x5C7E44 (@0x5c7ed6) et
//       UI_Shop_ShowItemTooltip 0x5C9360 (@0x5c9386).
//   unk_9404B0 -> (0x9404B0-0x8E8B50)/148 = 2424, reste 0 -> 001_02425.IMG
//       Tuile de fond d'infobulle 13x15 (Item_DrawTooltip 0x652AD0 @0x65e305),
//       consommée par UI/ItemTooltip.cpp via le constructeur `Cat1Slot` ci-dessous.
//
// TODO [ancres 0x5DA210 / 0x5C7E44 / … ] — CÂBLAGE HORS DE CE FICHIER : les 11
// littéraux `PanelSkin kPanelBg("…001_XXXXX.IMG")` vivent dans des .cpp de fenêtres
// (GuildWindow.cpp:23, VendorShopWindow.cpp:62, CharacterStatsWindow.cpp:28,
// SocialWindow.cpp:17, SkillTreeWindow.cpp:63, PlayerTradeWindow.cpp:27,
// QuestTrackerWindow.cpp:29, PartyWindow.cpp:25, NpcDialogWindow.cpp:28,
// OptionsWindow.cpp:18, AutoPlayWindow.cpp:29) et restent des DEVINETTES tant
// qu'ils n'ont pas été remplacés par `PanelSkin::Cat1Slot{<slot prouvé>}`. La
// méthode à appliquer pour chacun : décompiler son vrai UI_*_Draw, repérer l'appel
// de FOND `Sprite2D_Draw(&unk_X, *this, *(this+1))` précédé du centrage
// `Sprite2D_GetWidth/Height(&unk_X)`, calculer (unk_X - 0x8E8B50)/148 et vérifier
// que le reste vaut 0. Seuls 406 / 2735 / 2424 sont prouvés à ce jour.
#pragma once
#include "UI/UIManager.h"
#include "Gfx/GpuTexture.h"
#include <string>

namespace ts2::ui {

// ---------------------------------------------------------------------------
// Miroir de g_UseTRVariant (dword_1669190), initialisé à 0 par WinMain @0x4609FB
// et mis à 1 quand le champ 1 de la ligne de commande vaut 1 (cf. App/GameConfig.h
// `useTRVariant`, App.cpp:83). Consulté par Sprite2D_BuildPath 0x4D6900 pour les
// catégories 1 (@0x4d6913) et 4 (@0x4d6999) UNIQUEMENT — les catégories 2/3/5/6/7
// n'ont pas de branche TR (gap TEX-2).
//
// TODO [ancre 0x4609FB] — CÂBLAGE HORS DE CE FICHIER : App::Init doit appeler
// `ts2::ui::SetUseTRVariant(cfg_.useTRVariant != 0);` juste après le parse de la
// ligne de commande (App/App.cpp, à côté de la ligne 363
// `gfx::Font::AddTtfResource(cfg_.useTRVariant != 0)`, qui applique déjà le même
// champ aux polices). Tant que ce hook n'est pas posé, la valeur reste `false`
// = branche NON-TR = comportement EXACT du build EU par défaut (`/0/0/2/…` donne
// field[1]=0), donc aucune régression ; seul un lancement TR resterait incohérent.
void SetUseTRVariant(bool on);

// Construit le chemin d'un fichier de l'atlas UI catégorie 1 à partir de son
// index de SLOT (0-based) — réplique exacte des deux branches de
// Sprite2D_BuildPath 0x4D6900 case 1 (@0x4d6928 / @0x4d6945), numéro de fichier
// = slot + 1. Renvoie une chaîne vide si `slot` est négatif.
std::string Cat1SlotPath(int slot);

// ---------------------------------------------------------------------------
// Fond de panneau texturé paresseux (1 tentative de chargement, mémorisée).
// Deux façons de le construire :
//   static const PanelSkin s_bg(PanelSkin::Cat1Slot{2424});   // PRÉFÉRÉ (prouvé)
//   static const PanelSkin s_bg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01165.IMG");
// La forme `Cat1Slot` est la seule fidèle : elle passe par Cat1SlotPath() et
// honore donc la variante TR, exactement comme le binaire.
class PanelSkin {
public:
    // Index de slot 0-based dans le pool d'atlas UI catégorie 1 (base 0x8E8B50,
    // 4500 slots) — cf. le bandeau de tête pour la dérivation.
    struct Cat1Slot { int slot; };

    explicit PanelSkin(const char* imgRelPath) : path_(imgRelPath ? imgRelPath : "") {}
    explicit PanelSkin(Cat1Slot s) : slot_(s.slot) {}

    // ---- Blit à TAILLE NATURELLE (fidèle) --------------------------------
    // Sprite2D_Draw 0x4D6B20 appelle UI_DrawSprite(this+104, x, y, 0,0,0,0,0)
    // (@0x4D6B72) : AUCUN facteur d'échelle. C'est la primitive utilisée par tous
    // les fonds de panneau et par la tuile d'infobulle. Dessine la texture à sa
    // taille réelle en (x,y) et renvoie true ; renvoie false SANS RIEN dessiner si
    // la texture est indisponible (l'appelant décide de son propre repli). Phase
    // Panels uniquement.
    bool Draw(const UiContext& ctx, int x, int y) const;

    // Taille naturelle de la texture (0 si indisponible). Permet aux fenêtres de
    // DÉRIVER leur géométrie du sprite au lieu de l'inventer, comme
    // UI_Shop_ShowItemTooltip 0x5C9360 :
    //     *this     = nWidth/2  - (u16)Sprite2D_GetWidth(&unk_94B87C)/2   @0x5c939d
    //     *(this+1) = nHeight/2 - (u16)Sprite2D_GetHeight(&unk_94B87C)/2  @0x5c93c2
    // Charge la texture au premier appel (comme Draw).
    uint32_t TexW(const UiContext& ctx) const;
    uint32_t TexH(const UiContext& ctx) const;

    // ---- Blit ÉTIRÉ (NON fidèle — conservé pour les 11 appelants existants) --
    // TODO [ancre 0x4D6B20] — DIVERGENCE PROUVÉE (gap TT-06), correctif HORS DE CE
    // FICHIER. Cette surcharge étire la texture vers un rect (w,h) que chaque
    // fenêtre invente (constantes kPanelW/kPanelH de son .h) via
    // SpriteBatch::DrawSpriteScaled. Le binaire fait l'INVERSE : la taille de la
    // fenêtre EST celle du sprite (cf. 0x5c939d/0x5c93c2 ci-dessus), et le blit
    // n'a pas d'échelle (0x4D6B72). Une primitive d'échelle existe bien
    // (Sprite2D_DrawScaled 0x4D6BF0) mais n'est PAS celle des panneaux.
    // Elle est CONSERVÉE TELLE QUELLE parce que les 11 fenêtres qui l'appellent
    // ancrent leurs widgets sur kPanelW/kPanelH : la supprimer (ou la faire blitter
    // à taille naturelle) désalignerait tout leur contenu sans que ce front puisse
    // le corriger. Migration à faire fenêtre par fenêtre, chacune dans son .cpp :
    // remplacer `Draw(ctx, x, y, w, h, col)` par `Draw(ctx, x, y)`, dériver
    // l'origine via TexW/TexH ((ctx.screenW - TexW)/2, (ctx.screenH - TexH)/2,
    // divisions entières) et supprimer kPanelW/kPanelH.
    bool Draw(const UiContext& ctx, int x, int y, int w, int h, D3DCOLOR fallbackColor) const;

private:
    void EnsureLoaded(const UiContext& ctx) const;

    mutable std::string      path_;          // chemin résolu (ou fourni tel quel)
    int                      slot_ = -1;     // >= 0 : résolu paresseusement via Cat1SlotPath
    mutable gfx::GpuTexture  tex_;
    mutable bool             tried_ = false; // chargement tenté (succès ou échec) une seule fois
};

} // namespace ts2::ui

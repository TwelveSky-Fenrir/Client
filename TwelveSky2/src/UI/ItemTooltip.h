// UI/ItemTooltip.h — infobulle d'objet. Transposition de Item_DrawTooltip 0x652AD0.
//
// -----------------------------------------------------------------------------
// CORRECTION DE PRÉMISSE (révision W9) — l'ancien bandeau de ce fichier justifiait
// un widget de libre invention en affirmant que « le client d'origine n'affiche
// AUCUNE infobulle d'objet ». C'est FACTUELLEMENT FAUX, et cette erreur servait
// d'excuse à des libellés français en dur, des D3DCOLOR littéraux et un FillRect
// de fond. Vérité terrain :
//   - Item_DrawTooltip 0x652AD0 EXISTE et a 14+ appelants (`xrefs_to` : Shop,
//     Warehouse, ShareBox, Consign, ItemList, Storage x3, Refine, NpcShop,
//     SkillBoard, CharSelect x3...).
//   - Il est atteint CHAQUE FRAME : UI_RenderAllDialogs 0x5AE2D0 se termine par un
//     tail call INCONDITIONNEL `call UI_RouteRButtonExamine` @0x5ae5c9 suivi de
//     `mov esp,ebp / pop ebp / retn` @0x5ae5ce — aucune garde.
//   - UI_RouteRButtonExamine 0x5AE5E0 (1 seul appelant : 0x5ae5c9) est une chaîne
//     premier-répondant de ~35 maillons ; chaque feuille de survol
//     (UI_Shop_ShowItemTooltip 0x5C9360 @0x5ae6b1, UI_Warehouse_ShowItemTooltip
//     0x5CB4A0 @0x5ae702, cGameHud_DrawTooltipDispatch 0x64EA30 @0x5ae7a4,
//     cQuickSlotWin_DrawTooltip 0x6620E0 @0x5ae7bf...) renvoie 1 si elle a dessiné.
//
// La reconstruction ci-dessous suit donc le binaire, PAS l'ergonomie : géométrie
// tuilée réelle, libellés issus de 005.DAT, couleurs par INDEX de palette. Les cas
// du switch principal qui ne sont pas prouvés ligne à ligne restent des TODO
// explicites plutôt que des inventions (cf. UI/ItemTooltip.cpp).
//
// -----------------------------------------------------------------------------
// TODO [ancres 0x5AE2D0 / 0x5AE5E0] — CÂBLAGE HORS DE CE FICHIER (gap TT-01).
// `DrawItemTooltip` n'a AUCUN appelant : sans le hook ci-dessous il reste du code
// mort. UIManager::Render (UI/UIManager.cpp:286-327) calcule déjà `cx, cy` (l.295)
// et rend les dialogues en ordre inverse sur 2 phases, mais n'exécute jamais la
// passe finale de routage d'infobulle qui, dans le binaire, suit le rendu des
// dialogues. Hook à poser, DANS LES DEUX PHASES, juste après la boucle
// `for (auto it = dialogs_.rbegin(); ...)` de chaque passe (UIManager.cpp l.315 pour
// la passe Panels, l.325 pour la passe Text) :
//
//     // UI_RenderAllDialogs 0x5AE2D0 @0x5ae5c9 : tail call inconditionnel vers
//     // UI_RouteRButtonExamine 0x5AE5E0 (chaîne premier-répondant).
//     RouteItemTooltip(ctx_, cx, cy);
//
// où `RouteItemTooltip` interroge en ordre de routage chaque fenêtre à grille pour
// obtenir la cellule survolée, et s'arrête à la première qui répond — miroir de la
// chaîne de 0x5AE5E0. La résolution de cellule elle-même appartient à chaque
// fenêtre (InventoryWindow::InvCellAt existe déjà, InventoryWindow.cpp:227) ; côté
// HUD le binaire NE route PAS par cellule dans une passe générique :
// cGameHud_DrawTooltipDispatch 0x64EA30 résout Equip/Quiver/Loot/Inv lui-même et
// est gardé par visible (this[175]) + onglet (this[226]==1), tandis que le shop
// (UI_Shop_ShowItemTooltip 0x5C9360) est en survol PUR, sans garde de visibilité
// (seul `if (!*(this+2)) return 0`). Ces fichiers ne sont pas dans le périmètre de
// ce front : le routeur doit être écrit par le propriétaire d'UIManager.cpp.
#pragma once
#include "UI/UIManager.h"
#include "Game/GameDatabase.h"
#include "Game/GameState.h"
#include "Game/ItemSystem.h"
#include <cstdint>

namespace ts2::ui {

// Dessine l'infobulle de l'objet `itemId`, ancrée sur (x,y) [position curseur
// client]. `socketWord` = le mot d'attributs bit-packé de la cellule (grade/gemme/
// enchant) ; c'est l'arg_10 de Item_DrawTooltip, dont le prologue décode les QUATRE
// octets (Item_GetAttribByte0..3 0x545610/0x545640/0x545670/0x5456A0, EA 0x652B1C /
// 0x652B2D / 0x652B4B / 0x652B7C). Passer 0 pour un objet nu — c'est exactement ce
// que fait la boutique : `Item_DrawTooltip(x, y, itemId, 0, 0, 1, 0, 0, 0)` @0x5c9471.
//
// Ne dessine rien si `itemId` ne résout à aucun ITEM_INFO (MobDb_GetEntry @0x652afa,
// nullptr -> saut direct à la sortie @0x652b0e).
//
// L'ANCRAGE N'EST PAS CELUI D'UNE INFOBULLE « à droite/en bas du curseur » : le
// panneau est posé À GAUCHE du curseur et CENTRÉ VERTICALEMENT sur lui
// (`x -= cols*13` @0x65e262-0x65e26a, `y -= (rows*15)/2` @0x65e276-0x65e283), sans
// aucun clamp écran. Voir le .cpp.
//
// À appeler dans les DEUX phases (UiPhase::Panels puis UiPhase::Text) — comme tous
// les tracés de ctx, les primitives se filtrent elles-mêmes selon ctx.phase.
void DrawItemTooltip(const UiContext& ctx, int x, int y, uint32_t itemId,
                     uint32_t socketWord = 0);

// Commodité pour les fenêtres à grille : `InvCell::color` EST le mot d'attributs
// (cf. Net/ItemActionDispatch.cpp : `eq.socket = cell.color;`). No-op si la cellule
// est vide.
void DrawItemTooltip(const UiContext& ctx, int x, int y, const game::InvCell& cell);

} // namespace ts2::ui

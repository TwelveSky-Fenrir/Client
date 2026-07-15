// UI/ItemTooltip.h — infobulle d'objet réutilisable (survol de cellule d'inventaire).
//
// Fonction LIBRE (PAS une ts2::ui::Dialog) : une infobulle de survol n'a ni position
// propre, ni flag bOpen, ni gestion de clic — elle vit entièrement dans la frame
// courante, positionnée par l'appelant à partir du curseur. Chaque fenêtre à grille
// d'objets (InventoryWindow, WarehouseWindow, EnchantWindow, VendorWindow…) l'appelle
// à la toute fin de son propre Render(const UiContext&, cursorX, cursorY), DANS LES
// DEUX PHASES (Panels puis Text, cf. UIManager.h), une fois qu'elle a déterminé
// quelle cellule est survolée :
//
//     const int idx = InvCellAt(cursorX, cursorY);
//     if (idx >= 0 && !inv[idx].empty())
//         ts2::ui::DrawItemTooltip(ctx, cursorX, cursorY, inv[idx].itemId, &inv[idx]);
//
// Comme c'est le DERNIER appel du Render() du dialogue le plus "au-dessus"
// (UI_RenderAllDialogs 0x5AE2D0 rend en ordre inverse du routage, popups en dernier),
// l'infobulle se retrouve dessinée par-dessus tout le reste de la frame.
//
// Résout l'ITEM_INFO (Game/GameDatabase.h, struct 436 o byte-exacte, GetItemInfo)
// et affiche : nom (+ niveau d'enchantement si `cellOpt` fourni), niveau requis
// (itemLevel), quantité empilée (InvCell::flag) et les stats plates non nulles
// (extAttack/intAttack/extDefense/intDefense/maxHp/maxMp/accuracy/evasion/critRate).
//
// Décodage de l'enchant : InvCell::color est le MÊME mot bit-packé que
// EquipSlot::socket (cf. Net/ItemActionDispatch.cpp : `eq.socket = cell.color;`).
// Son octet 3 est le niveau d'enchantement (1..59), lu via Item_GetAttribByte3
// (Game/ItemSystem.h — Item_GetAttribByte0..3 0x545610/0x545640/0x545670/0x5456A0).
// On N'APPELLE PAS Item_GetEnchantStatDelta (0x553D50) ici : cette fonction attend un
// `itemClass` (résolu par Item_ClassifyRecord 0x5509A0, interne à StatFormulas.cpp,
// hors périmètre de ce fichier) et un `slot` D'ÉQUIPEMENT (0..12) qu'une InvCell de
// SAC générique (Warehouse/Inventory/Vendor) ne porte pas — les inventer produirait
// un delta chiffré non fidèle au binaire. Le niveau d'enchant brut décodé reste en
// revanche un affichage byte-exact et utile.
#pragma once
#include "UI/UIManager.h"
#include "Game/GameDatabase.h"
#include "Game/ItemSystem.h"
#include <cstdint>

namespace ts2::ui {

// Dessine l'infobulle de l'objet `itemId`, ancrée près de (x,y) [position curseur
// client], repositionnée pour ne jamais déborder de [0,ctx.screenW) x [0,ctx.screenH).
// `cellOpt` (optionnel) fournit le contexte de cellule d'inventaire (quantité,
// enchant décodé depuis son mot socket/durabilité) ; passer nullptr pour un
// affichage "objet nu" (ex. survol d'une recette/vitrine sans cellule concrète).
// Ne dessine rien si `itemId` ne résout à aucun ITEM_INFO (slot vide / id invalide).
// À appeler dans les deux phases (UiPhase::Panels puis UiPhase::Text) — comme tous
// les tracés de ctx, les primitives se filtrent elles-mêmes selon ctx.phase.
void DrawItemTooltip(const UiContext& ctx, int x, int y, uint32_t itemId,
                      const game::InvCell* cellOpt = nullptr);

} // namespace ts2::ui

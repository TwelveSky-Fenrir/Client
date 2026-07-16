// Game/ConsumableBarLogic.cpp — voir Game/ConsumableBarLogic.h pour la table de
// correspondance EA <-> fonction et le bandeau d'écart binaire/module.
//
// ⚠️ Ce fichier porte le PANNEAU 28 cases (UI_ConsumableBar_* 0x68E270+, grille 4x7,
// pas 52), PAS la barre du HUD (UI_GameHud_Render 0x67A3C0 @0x684CA8-0x685177, grille
// 1x14, pas 30, source g_Container5 0x16743FC) — cette dernière est portée par
// UI/ConsumableBarWindow.{h,cpp}, qui lit g_Container5 directement. Voir le bandeau
// « CE MODULE NE MODÉLISE PAS LA BARRE DU HUD » en tête du header avant d'appeler
// quoi que ce soit d'ici.
#include "Game/ConsumableBarLogic.h"

#include <cstddef>

#include "Game/GameState.h"     // game::InvCell
#include "Game/GameDatabase.h"  // game::GetItemInfo
#include "Game/ClientRuntime.h" // game::g_Client.inv (InventoryState) + g_Client.Var (byte_8013FE)

namespace ts2::game {

namespace {

// EA 0x68E2A4..0x68E2FE : les 10 premières des 14 valeurs du catalogue fixe
// d'origine (itemId de potions/scrolls). Les 4 suivantes (1241, 1244, 1242,
// 1243 — EA 0x68E308/0x68E312/0x68E31C/0x68E326) sont tronquées : le type
// ts2::ui::QuickSlot ne compte que ui::kQuickSlotCount (10) cases.
constexpr std::array<uint32_t, 10> kDefaultCatalog = {
    540, 565, 541, 542, 543, 544, 545, 546, 539, 1240,
};

} // namespace

void InitConsumableBar(ConsumableSlots& slots) {
    // EA 0x68E279..0x68E297 : zéro les 28 cases du binaire -> ici les
    // ui::kQuickSlotCount cases du tableau.
    for (auto& s : slots) s = ui::QuickSlot{};

    // EA 0x68E2A4..0x68E2FE (tronqué, voir kDefaultCatalog ci-dessus).
    for (std::size_t i = 0; i < slots.size() && i < kDefaultCatalog.size(); ++i) {
        slots[i].type  = ui::QuickSlotType::Item;
        slots[i].refId = kDefaultCatalog[i];
    }
}

int HitTestConsumableBar(const ConsumableSlots& slots, int originX, int originY,
                          int mouseX, int mouseY) {
    // EA 0x68EA2D..0x68EA65 : boucle jusqu'à la première case dont le
    // rectangle contient (mouseX, mouseY), ou fin de tableau.
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        const int col = i % kGridColumns;
        const int row = i / kGridColumns;
        const int x0 = originX + kGridStride * col + kGridCellX0;
        const int x1 = originX + kGridStride * col + kGridCellX1;
        const int y0 = originY + kGridStride * row + kGridCellY0;
        const int y1 = originY + kGridStride * row + kGridCellY1;
        if (mouseX >= x0 && mouseX <= x1 && mouseY >= y0 && mouseY <= y1) {
            // EA 0x68EAE3 : case touchée mais vide -> -1 quand même.
            return slots[static_cast<std::size_t>(i)].empty() ? -1 : i;
        }
    }
    return -1; // EA 0x68EAD3 : aucune case touchée.
}

ConsumableDecision TriggerSlot(const ConsumableSlots& slots, int index, bool rightClick) {
    ConsumableDecision d;
    d.slotIndex = index;
    if (index < 0 || index >= static_cast<int>(slots.size())) return d; // hors bornes -> None

    const ui::QuickSlot& slot = slots[static_cast<std::size_t>(index)];
    if (slot.empty()) return d; // None, aucune EA d'origine n'agit sur une case vide

    d.refId = slot.refId;

    if (slot.type == ui::QuickSlotType::Skill) {
        // Voir bandeau d'écart en tête de Game/ConsumableBarLogic.h : aucune
        // des 6 EA de ce module ne résout de SKILL_INFO.
        d.action   = ConsumableAction::Unsupported;
        d.consumed = true;
        return d;
    }

    // slot.type == Item.
    if (rightClick) {
        // EA 0x68E9B2 : Item_DrawTooltip(a2, a3, itemId, 0, 0, 2, 0, 0, 0),
        // appelé sans vérifier l'existence de l'entrée ITEM_INFO.
        d.action   = ConsumableAction::ShowTooltip;
        d.consumed = true;
        return d;
    }

    // EA 0x68E480..0x68E491 : MobDb_GetEntry(&mITEM, itemId).
    const ItemInfo* info = GetItemInfo(slot.refId);
    if (!info) {
        // EA 0x68E48F/0x68E491 : entrée introuvable -> clic consommé, aucun effet.
        d.action   = ConsumableAction::Invalid;
        d.consumed = true;
        return d;
    }

    d.action   = ConsumableAction::BeginItemDrag;
    d.consumed = true;

    if (info->typeCode == 2) { // EA 0x68E4A5 : *(v6+188) == 2
        // EA 0x68E4B4 : branche pilotée par byte_8013FE (signification exacte
        // TODO — non documentée ailleurs dans le désassemblage relevé pour
        // cette mission). Exposé fidèlement via l'échappatoire ClientRuntime.
        const bool negative = g_Client.VarGet(0x8013FEu) < 0;
        if (negative) {
            // EA 0x68E4E6 : Item_BeginDragTransaction(..., itemId, 0,0,0,0,0,
            // 1, a2-52, a3-72) -> drag avec prompt de quantité près du curseur.
            d.promptQuantity = true;
            d.dragCount      = 0;
        } else {
            // EA 0x68E513 : Item_BeginDragTransaction(..., itemId, 99, 0,0,0,0,
            // 0,0,0) -> drag d'une quantité fixe (pile pleine 99).
            d.promptQuantity = false;
            d.dragCount      = 99;
        }
    } else {
        // EA 0x68E540 : Item_BeginDragTransaction(..., itemId, 0,0,0,0,0,0,0,0).
        d.promptQuantity = false;
        d.dragCount      = 0;
    }

    d.usable = InventoryCount(slot.refId) > 0; // extension mission, hors EA
    return d;
}

ConsumableDecision OnClick(ConsumableBarState& state, const ConsumableSlots& slots,
                            int originX, int originY, int mouseX, int mouseY,
                            bool closeButtonHit, bool parentHudConsumed) {
    ConsumableDecision d;

    if (!state.visible) { // EA 0x68E3CD
        d.action = ConsumableAction::Ignored;
        return d; // consumed=false : évènement non consommé
    }

    // EA 0x68E433 : cGameHud_OnMouseDown court-circuite avant tout hit-test.
    // Sous-système HUD hors périmètre ici — reproduit via le paramètre.
    if (parentHudConsumed) {
        d.consumed = true;
        return d;
    }

    const int slot = HitTestConsumableBar(slots, originX, originY, mouseX, mouseY);
    if (slot != -1) {
        d = TriggerSlot(slots, slot, /*rightClick=*/false);
        if (d.action == ConsumableAction::BeginItemDrag && d.promptQuantity) {
            d.dragCursorX = mouseX - 52; // EA 0x68E4E6
            d.dragCursorY = mouseY - 72;
        }
        return d;
    }

    // EA 0x68E45D..0x68E596 : pas de case pleine touchée -> test du bouton
    // fermer. Le rectangle réel dépend de la taille du sprite unk_8F3798
    // (Sprite2D_HitTest 0x4D6C50, EA 0x68E56C) : hors périmètre logique,
    // fourni par l'appelant (rendu) via `closeButtonHit`.
    if (closeButtonHit) {
        state.closeButtonArmed = true; // EA 0x68E588
        d.action   = ConsumableAction::ArmCloseButton;
        d.consumed = true;
        return d;
    }

    return d; // EA 0x68E596 : None, non consommé
}

ConsumableDecision OnMouseUp(ConsumableBarState& state, bool closeButtonHit,
                              bool parentHudConsumed) {
    ConsumableDecision d;

    if (!state.visible) { // EA 0x68E5AB
        d.action = ConsumableAction::Ignored;
        return d;
    }

    if (parentHudConsumed) { // EA 0x68E611 : cGameHud_OnMouseUp
        d.consumed = true;
        return d;
    }

    if (!state.closeButtonArmed) return d; // EA 0x68E624 : retour 0, non consommé

    state.closeButtonArmed = false; // EA 0x68E62D

    if (closeButtonHit) {
        // EA 0x68E654/0x68E667 -> sub_68E3A0(this) : remet *(this+2) à 0.
        state.visible  = false;
        d.action       = ConsumableAction::ClosePanel;
    }

    d.consumed = true; // EA 0x68E675 : toujours 1 une fois le bouton armé
    return d;
}

ConsumableDecision OnRightClick(const ConsumableBarState& state, const ConsumableSlots& slots,
                                 int originX, int originY, int mouseX, int mouseY,
                                 bool tooltipDispatchConsumed) {
    ConsumableDecision d;

    if (!state.visible) return d; // EA 0x68E94C : non consommé

    if (tooltipDispatchConsumed) { // EA 0x68E963 : cGameHud_DrawTooltipDispatch
        d.consumed = true;
        return d;
    }

    const int slot = HitTestConsumableBar(slots, originX, originY, mouseX, mouseY);
    if (slot == -1) return d; // EA 0x68E98A : non consommé

    return TriggerSlot(slots, slot, /*rightClick=*/true); // EA 0x68E9B2
}

uint32_t InventoryCount(uint32_t itemId) {
    // Modèle unique game::g_Client.inv (InventoryState, Game/ClientRuntime.h) —
    // PAS game::g_World.self.inventory (ancien « modèle simplifié », retiré lors de
    // la réconciliation des deux modèles d'inventaire concurrents, mission
    // « inventaire », 2026-07-14). Balaie toutes les cellules (toutes pages
    // confondues) : bon marché (2048 InvCell) et sans risque de sous-compter un
    // objet rangé sur une page actuellement non affichée par l'UI.
    uint32_t total = 0;
    for (const InvCell& cell : g_Client.inv.cells) {
        if (cell.itemId == itemId) total += cell.flag; // flag = compteur de pile
    }
    return total;
}

bool IsSlotUsable(const ConsumableSlots& slots, int index) {
    if (index < 0 || index >= static_cast<int>(slots.size())) return false;
    const ui::QuickSlot& slot = slots[static_cast<std::size_t>(index)];
    if (slot.empty()) return false;

    if (slot.type == ui::QuickSlotType::Skill) {
        // TODO : nécessite Game/SkillSystem.h (hors contrat de headers) et la
        // vraie fonction de cooldown/hotkey, non localisée pour cette mission.
        return false;
    }

    const ItemInfo* info = GetItemInfo(slot.refId);
    if (!info) return false;
    return InventoryCount(slot.refId) > 0;
}

ConsumableDecision TriggerSlotByHotkey(const ConsumableSlots& slots, uint8_t dikScanCode) {
    // DIK_1..DIK_9 = 0x02..0x0A, DIK_0 = 0x0B (cf. Input/InputSystem.h,
    // UI/GameHud.h lignes 27-30, Docs/TS2_CLIENT_SHELL.md §4). Convention déjà
    // établie ailleurs dans ce codebase — PAS une traduction d'EA de ce module
    // (aucune des 6 EA du ConsumableBar ne lit le clavier).
    if (dikScanCode < 0x02 || dikScanCode > 0x0B) return ConsumableDecision{};
    const int index = static_cast<int>(dikScanCode) - 0x02; // DIK_1->0 .. DIK_0->9
    return TriggerSlot(slots, index, /*rightClick=*/false);
}

} // namespace ts2::game

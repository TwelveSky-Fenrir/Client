// UI/ConsumableBarWindow.h — rendu + interaction de la barre de consommables
// (quickslots), contrepartie PIXEL de Game/ConsumableBarLogic.h (qui fournit
// explicitement la logique de déclenchement mais PAS le rendu — voir son
// bandeau de tête). Barre TOUJOURS VISIBLE en bas d'écran, 10 cases (touches
// 1..9 puis 0, DIK 0x02..0x0B), donc PAS un ts2::ui::Dialog modal : rien à
// router via UIManager, l'appelant (Scene InGame / GameHud) pousse
// directement les événements souris/clavier et appelle Render() chaque frame.
//
// Source de vérité des cases = std::array<ts2::ui::QuickSlot,10> fourni par
// l'appelant (ex. GameHud::Slot(i)) : ce module ne possède PAS les slots, il
// les affiche et décide de l'action via game::TriggerSlot /
// game::TriggerSlotByHotkey (Game/ConsumableBarLogic.h), qui sont précisément
// l'extension prévue par ce header pour un hotbar 10 cases piloté par
// l'inventaire (cf. son commentaire de tête, section « Extensions demandées
// par la mission »).
#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "UI/UIManager.h"           // ts2::ui::UiContext
#include "UI/GameHud.h"             // ts2::ui::QuickSlot, kQuickSlotCount
#include "Game/ConsumableBarLogic.h" // ts2::game::TriggerSlot / TriggerSlotByHotkey / decisions
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"    // cache GPU d'icônes .IMG (pattern InventoryWindow/VendorShopWindow)

namespace ts2::ui {

// Petite classe utilitaire (PAS un Dialog : toujours visible, hors chaîne de
// routage UIManager). Conserve juste le dernier layout écran (pour aligner
// hit-test et rendu, comme MsgBoxDialog::Layout) et le dernier message de
// retour à afficher au-dessus de la barre.
class ConsumableBarWindow {
public:
    ConsumableBarWindow() = default;

    // Rendu (appelé DEUX FOIS par frame par le pipeline UI : phase Panels puis
    // Text — voir UIManager.h). `slots` = source de vérité (ex. GameHud).
    // `cursorX`/`cursorY` optionnels : si fournis (>=0), surlignent la case
    // survolée ; signature compatible avec l'appel minimal demandé
    // Render(ctx, slots) grâce à ces valeurs par défaut.
    void Render(const UiContext& ctx, const std::array<QuickSlot, kQuickSlotCount>& slots,
                int cursorX = -1, int cursorY = -1);

    // Clic-gauche enfoncé : arme le latch si le curseur est sur la barre
    // (bloque le passage du clic à la scène 3D derrière le HUD, comme
    // GameHud::OnMouseDown / InventoryWindow::OnMouseDown). N'exécute AUCUNE
    // action — l'action se décide au relâchement (OnClick), comme le pattern
    // Button de Widgets.h.
    bool OnMouseDown(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Clic-gauche relâché = clic validé -> game::TriggerSlot(slots, index).
    // Renvoie true si l'événement est consommé (case pleine ou vide sous le
    // curseur, ou zone de la barre). Le résultat est mémorisé (LastDecision())
    // et un message est préparé pour le prochain Render().
    bool OnClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Clic-droit = tooltip -> game::TriggerSlot(slots, index, /*rightClick=*/true).
    bool OnRightClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Raccourci clavier direct (DIK_1..DIK_9=0x02..0x0A, DIK_0=0x0B), pour
    // brancher sur Input/InputSystem.h sans passer par la souris.
    bool OnHotkey(uint8_t dikScanCode, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Dernière décision calculée (BeginItemDrag/Invalid/Unsupported/...).
    const game::ConsumableDecision& LastDecision() const { return lastDecision_; }
    // Dernier message affiché au-dessus de la barre (log/retour visuel).
    const std::string& LastMessage() const { return lastMessage_; }

    // Cache GPU d'icônes PARTAGÉ (mutualisation mémoire, cf. Gfx/IconTextureCache.h) :
    // à injecter par l'appelant avec la même instance qu'InventoryWindow/WarehouseWindow/
    // EnchantWindow/VendorShopWindow (UI/GameWindows.cpp) si un point de câblage devient
    // disponible pour ce widget (aujourd'hui possédé par GameHud, hors de GameWindows).
    // nullptr (repli, cas courant aujourd'hui) => ownIconCache_ locale, cf. InventoryWindow.
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

private:
    struct SlotRect {
        int x = 0, y = 0, w = 0, h = 0;
        bool Contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };
    struct Layout {
        SlotRect bar; // fond de la barre (englobe les 10 cases)
        std::array<SlotRect, kQuickSlotCount> cells{};
    };

    // Recalcule la géométrie (bas d'écran, centré) à partir des dims courantes.
    static Layout ComputeLayout(int screenW, int screenH);

    // Hit-test sur le DERNIER layout dessiné (aligne rendu et clic, comme
    // MsgBoxDialog::lastScreenW_/lastScreenH_ : le clic est routé entre deux
    // frames, donc on réutilise la géométrie effectivement affichée).
    int HitTest(int mx, int my) const;

    // Applique une décision de game::ConsumableBarLogic : mémorise le message
    // à afficher (armé pour le prochain Render(), qui connaît gameTimeSec).
    void ApplyDecision(const game::ConsumableDecision& d, int index,
                        const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Icône réelle d'un objet (chargement paresseux + cache, pattern
    // InventoryWindow::GetIconTex/ResolveItemIconPath — ITEM_INFO::iconId, PAS
    // itemId, gabarit "G03_GDATA\D01_GIMAGE2D\002\002_%05u.IMG"). nullptr si
    // ctx.renderer/Device absent, itemId hors ITEM_INFO, ou chargement en échec
    // (repli sur la case pleine colorée déjà dessinée par Render()).
    gfx::GpuTexture* GetIconTex(const UiContext& ctx, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    Layout lastLayout_{};
    int    lastScreenW_ = ts2::kRefWidth;
    int    lastScreenH_ = ts2::kRefHeight;

    game::ConsumableDecision lastDecision_{};
    std::string lastMessage_;
    D3DCOLOR    lastMessageColor_ = 0xFFFFFFFFu;
    bool        messagePending_   = false; // armé par OnClick/OnRightClick/OnHotkey, consommé par Render
    float       messageUntilSec_  = -1.0f; // gameTimeSec au-delà duquel le message s'efface

    // Cache icônes (chemin de fichier -> texture GPU) : voir SetIconCache()/
    // ActiveIconCache() ci-dessus. ownIconCache_ = repli local (utilisé tant
    // qu'aucun appelant n'injecte le cache partagé de GameWindows).
    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;
};

} // namespace ts2::ui

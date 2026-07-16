// UI/ConsumableBarWindow.h — rendu + interaction de la barre de raccourcis du HUD
// (« quickbar »), portage de UI_GameHud_Render 0x67A3C0, bloc 0x684CA8-0x685177.
//
// =============================================================================
// SOURCE DE VÉRITÉ DES CASES (corrigé vague W9, 2026-07-16) — LIRE AVANT USAGE
// =============================================================================
// Ce widget NE LIT PLUS le paramètre `slots` : il lit `g_Container5` comme le
// binaire. Motif (prouvé par Grep exhaustif + IDA) :
//
//  1. `GameHud::slots_` (std::array<QuickSlot,10>, GameHud.h:270) n'a AUCUN
//     écrivain dans tout src/ : 6 lectures, 0 écriture ; `GameHud::Slot(int)`
//     (accesseur non-const) n'a AUCUN appelant ; `game::InitConsumableBar` n'a
//     AUCUN appelant. `slots_` reste donc {Empty×10} à vie -> avec l'ancien
//     rendu, `s.empty()` était TOUJOURS vrai et la barre ne dessinait que des
//     cadres vides. Tout le reste du rendu était inatteignable (code mort).
//
//  2. Le binaire ne consulte jamais de catalogue privé. Il lit `g_Container5`,
//     un bloc de 3 pages x 14 slots x 3 dwords (stride page 0xA8 = 168 = 14*12,
//     stride slot 0xC) :
//       itemId/skillId  0x16743FC + 0xA8*page + 0xC*i   [0x684E55, 0x684ECF,
//                                                        0x684FE7, 0x685061]
//       compteur        0x1674400 + 0xA8*page + 0xC*i   [0x684EB2, 0x684F70,
//                                                        0x6850E4, 0x685103]
//       type de slot    0x1674404 + 0xA8*page + 0xC*i   [0x684E0B -> var_8D4]
//       page courante   dword_1675B1C                   [0x684D85, 0x684DF6]
//       slot sélect.    dword_1675B20                   [0x684E81]
//     Boucle 0x684DE9 : `cmp var_438, 0Eh / jge` -> **14 slots**, pas 10.
//
//  3. Cette source EST déjà alimentée par le réseau côté C++ :
//     Net/GameHandlers_InvCells.cpp:462-464 écrit les trois dwords via
//     `g_Client.Var(...)` ; purge concurrente Net/CharStatDeltaDispatch.cpp:584-587.
//
// Conséquence : le paramètre `slots` des méthodes publiques est CONSERVÉ (la
// signature reste compatible avec les appels existants de GameHud.cpp:1127/1184/
// 1313-1314 — aucune retouche de GameHud.cpp requise) mais N'EST PLUS LU par
// Render(). Il reste lu par OnClick/OnRightClick/OnHotkey, voir ci-dessous.
//
// `ui::kQuickSlotCount` (=10, GameHud.h:62) contredit le binaire (14) et
// `QuickSlotType` (GameHud.h:65) ignore le type 2 (voir kSlotType* ci-dessous) :
// en lisant `g_Container5` en interne on contourne les deux SANS toucher à
// GameHud.h (hors périmètre de ce front). Signalé à l'orchestrateur.
//
// =============================================================================
// CE QUE LE BINAIRE DESSINE (et donc ce que ce widget dessine) — 0x684D40..0x685177
// =============================================================================
// UNIQUEMENT des blits d'atlas (`Sprite2D_Draw` x4) et deux nombres
// (`UI_DrawNumberValue` x2). AUCUN rectangle coloré, AUCUN cadre, AUCUN survol,
// AUCUN message de retour : tout cela a été retiré du rendu (c'était inventé).
// La barre est TOUJOURS VISIBLE en bas d'écran, donc PAS un ts2::ui::Dialog :
// rien à router via UIManager, l'appelant (GameHud) pousse les événements.
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

// Nombre RÉEL de cases de la barre HUD — boucle 0x684DE9 `cmp var_438, 0Eh / jge
// loc_68515A`. Volontairement local à ce widget : `ui::kQuickSlotCount` (=10)
// décrit un AUTRE modèle (voir bandeau de tête) et n'est pas modifiable depuis
// ce front.
inline constexpr int kBarSlotCount = 14;

// Valeurs du dword « type de slot » (`dword_1674404`, switch 0x684E18-0x684E35).
// Le binaire a TROIS branches ; `ui::QuickSlotType` n'en connaît que deux
// (Empty/Item/Skill), d'où l'usage de la valeur brute ici.
inline constexpr int32_t kSlotTypeSkill  = 1; // -> loc_684E40 (SKILL_INFO, atlas cat.3)
inline constexpr int32_t kSlotTypeTabIcon= 2; // -> loc_684FD3 (cQuickSlotWin_GetTabIcon 0x662750)
inline constexpr int32_t kSlotTypeItem   = 3; // -> loc_68504C (ITEM_INFO, atlas cat.2)

// Petite classe utilitaire (PAS un Dialog : toujours visible, hors chaîne de
// routage UIManager). Conserve juste le dernier layout écran (pour aligner
// hit-test et rendu, comme MsgBoxDialog::Layout).
class ConsumableBarWindow {
public:
    ConsumableBarWindow() = default;

    // Rendu (appelé DEUX FOIS par frame par le pipeline UI : phase Panels puis
    // Text — voir UIManager.h). `slots` est IGNORÉ (voir bandeau de tête : la
    // source réelle est g_Container5) ; conservé pour compatibilité d'appel.
    // `cursorX`/`cursorY` sont ignorés eux aussi : le binaire ne dessine aucun
    // survol sur cette barre.
    void Render(const UiContext& ctx, const std::array<QuickSlot, kQuickSlotCount>& slots,
                int cursorX = -1, int cursorY = -1);

    // Clic-gauche enfoncé : arme le latch si le curseur est sur la barre
    // (bloque le passage du clic à la scène 3D derrière le HUD, comme
    // GameHud::OnMouseDown / InventoryWindow::OnMouseDown). N'exécute AUCUNE
    // action — l'action se décide au relâchement (OnClick), comme le pattern
    // Button de Widgets.h.
    bool OnMouseDown(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Clic-gauche relâché = clic validé -> game::TriggerSlot(slots, index).
    // Renvoie true si l'événement est consommé.
    bool OnClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Clic-droit = tooltip -> game::TriggerSlot(slots, index, /*rightClick=*/true).
    bool OnRightClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Raccourci clavier direct (DIK_1..DIK_9=0x02..0x0A, DIK_0=0x0B), pour
    // brancher sur Input/InputSystem.h sans passer par la souris.
    bool OnHotkey(uint8_t dikScanCode, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Dernière décision calculée (BeginItemDrag/Invalid/Unsupported/...).
    const game::ConsumableDecision& LastDecision() const { return lastDecision_; }
    // Dernier message calculé par le chemin de clic. N'EST PLUS DESSINÉ par
    // Render() (le binaire n'affiche aucun message au-dessus de la barre) :
    // conservé pour l'appelant/log (GameHud.cpp:1317 le journalise déjà).
    const std::string& LastMessage() const { return lastMessage_; }

    // Cache GPU d'icônes PARTAGÉ (mutualisation mémoire, cf. Gfx/IconTextureCache.h).
    // Côté binaire, AssetMgr_InitAllSlots 0x4DEB50 construit UN SEUL tableau global de
    // slots Sprite2D (base 0x8E8B30) : HUD/inventaire/entrepôt/boutique l'indexent tous
    // -> chaque .IMG est chargé UNE fois. Pour reproduire cette mutualisation, l'appelant
    // doit injecter ici la même instance qu'InventoryWindow/WarehouseWindow/EnchantWindow/
    // VendorShopWindow (UI/GameWindows.cpp:36-39).
    // nullptr (repli, cas courant aujourd'hui : GameHud possède ce widget et n'a pas de
    // pointeur vers le cache de GameWindows) => ownIconCache_ locale. Voir TEX-1 dans le
    // rapport de front : câblage à poser en UI/GameHud.cpp:419 (hors de ce front).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

private:
    struct SlotRect {
        int x = 0, y = 0, w = 0, h = 0;
        bool Contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };
    struct Layout {
        SlotRect bar; // fond de la barre (englobe les 14 cases)
        std::array<SlotRect, kBarSlotCount> cells{};
    };

    // Une case telle que le binaire la lit dans g_Container5 (3 dwords).
    struct LiveSlot {
        int32_t refId = 0; // dword_16743FC[...] : itemId ou skillId
        int32_t count = 0; // dword_1674400[...] : compteur de pile / charges
        int32_t type  = 0; // dword_1674404[...] : kSlotType* (0 = case vide)
    };

    // Lit les 3 dwords du slot `i` de la page `page` via game::g_Client.Var
    // (échappatoire d'adresse d'origine, cf. Game/ClientRuntime.h).
    // Ancre : 0x684DF6-0x684E12 (calcul d'adresse `0xA8*page + 0xC*i`).
    static LiveSlot ReadContainer5(int page, int i);

    // Recalcule la géométrie (bas d'écran, centré) à partir des dims courantes.
    static Layout ComputeLayout(int screenW, int screenH);

    // Hit-test sur le DERNIER layout dessiné (aligne rendu et clic, comme
    // MsgBoxDialog::lastScreenW_/lastScreenH_ : le clic est routé entre deux
    // frames, donc on réutilise la géométrie effectivement affichée).
    int HitTest(int mx, int my) const;

    // Applique une décision de game::ConsumableBarLogic : mémorise le message
    // (log/appelant uniquement — plus dessiné, cf. LastMessage()).
    void ApplyDecision(const game::ConsumableDecision& d, int index,
                        const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Texture d'un slot de la table Sprite2D globale, par CATÉGORIE et numéro de
    // fichier 1-based — reproduit Sprite2D_BuildPath 0x4D68E0 :
    //   cat.1 "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"  (atlas UI générique, 4500)
    //   cat.2 "G03_GDATA\D01_GIMAGE2D\002\002_%05d.IMG"  (icônes d'objets, 4000)
    //   cat.3 "G03_GDATA\D01_GIMAGE2D\003\003_%05d.IMG"  (icônes de compétences, 760)
    // Le numéro de fichier vaut `slotIndex + 1` (paramètre `a3 + 1` du binaire).
    // TODO [g_UseTRVariant 0x1669190, Sprite2D_BuildPath 0x4D6913] : les catégories
    // 1 et 4 ont une variante "\TR\" quand g_UseTRVariant==1 ; ce drapeau n'est pas
    // modélisé côté C++ (build EU) -> chemin non-TR seul, comme
    // UI/BuffStatusPanel.cpp::GxdCategory1Path.
    gfx::GpuTexture* GetCatTex(const UiContext& ctx, int category, int fileNo);
    // Icône réelle d'un objet : ITEM_INFO::iconId (ITEM_INFO+192, 1-based), atlas
    // catégorie 2. Le binaire blitte `g_AssetMgr_ItemIconSlots + 148*(iconId-1)`
    // (0x68508D/0x6850C5) -> slot iconId-1 -> fichier (iconId-1)+1 = iconId.
    gfx::GpuTexture* GetIconTex(const UiContext& ctx, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    Layout lastLayout_{};
    int    lastScreenW_ = ts2::kRefWidth;
    int    lastScreenH_ = ts2::kRefHeight;

    game::ConsumableDecision lastDecision_{};
    std::string lastMessage_;

    // Cache icônes (chemin de fichier -> texture GPU) : voir SetIconCache()/
    // ActiveIconCache() ci-dessus. ownIconCache_ = repli local (utilisé tant
    // qu'aucun appelant n'injecte le cache partagé de GameWindows).
    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;
};

} // namespace ts2::ui

// Game/ConsumableBarLogic.h — logique d'interaction du PANNEAU DE CONSOMMABLES
// 28 cases (assignation/déclenchement de slot, PAS le rendu pixel).
//
// =============================================================================
// ⚠️ CE MODULE NE MODÉLISE **PAS** LA BARRE DU HUD (précision W9, 2026-07-16)
// =============================================================================
// Deux objets DISTINCTS du binaire ont longtemps été confondus ici :
//
//   (a) le PANNEAU 28 cases        — UI_ConsumableBar_* 0x68E270+ : grille 4 x 7,
//       pas 52 px, catalogue FIXE d'items, bouton fermer, garde de visibilité
//       `*(this+2)`, ouvre un DRAG (Item_BeginDragTransaction 0x5AFDF0). C'est CE
//       module. Son RENDU (UI_ConsumableBar_Render 0x68E6E0) n'est pas porté, et
//       son appelant non plus : les fonctions ci-dessous n'ont donc AUCUN appelant
//       réel aujourd'hui, à l'exception de TriggerSlot/TriggerSlotByHotkey, appelées
//       par UI/ConsumableBarWindow (voir ci-dessous).
//
//   (b) la BARRE DU HUD (« quickbar ») — UI_GameHud_Render 0x67A3C0, bloc
//       0x684CA8-0x685177 : grille 1 x 14, pas 30 px, PAS de catalogue privé (elle
//       lit `g_Container5` @0x16743FC : 3 pages x 14 slots x 3 dwords), 3 types de
//       slot (compétence/onglet/objet), overlay de recharge, numéro de page. Elle
//       est portée par UI/ConsumableBarWindow.{h,cpp}, qui lit désormais
//       `g_Container5` DIRECTEMENT et n'utilise plus ce module pour son rendu.
//
// Conséquence pratique : les constantes de grille (kGridColumns=4, kGridStride=52…),
// `InitConsumableBar` (catalogue fixe), `HitTestConsumableBar`, `OnClick`,
// `OnMouseUp`, `OnRightClick` et `ConsumableBarState` décrivent (a) et NE DOIVENT PAS
// être utilisés pour (b). Ils restent ici, sans appelant, en attendant le portage du
// panneau 28 cases — un stub du binaire reste un stub (on ne supprime pas un portage
// fidèle parce que son appelant n'est pas encore écrit).
//
// Le chemin de clic RÉEL de la barre HUD est cGameHud_OnMouseDown 0x62B080 : NON
// reversé à ce jour. `UI/ConsumableBarWindow::OnClick` continue donc d'appeler
// `TriggerSlot` ci-dessous faute de mieux — c'est un pis-aller documenté, PAS une
// preuve pour la barre HUD.
//
// -----------------------------------------------------------------------------
// Réécriture C++ PROPRE mais fidèle des fonctions suivantes du binaire non pické
// TwelveSky2.exe (imagebase 0x400000, IDB idaTs2) :
//   UI_ConsumableBar_Init         0x68E270  -> InitConsumableBar
//   UI_ConsumableBar_HitTest      0x68E9D0  -> HitTestConsumableBar
//   UI_ConsumableBar_OnClick      0x68E3C0  -> OnClick (+ TriggerSlot)
//   UI_ConsumableBar_ProcInput    0x68E5A0  -> OnMouseUp
//   UI_ConsumableBar_OnRightClick 0x68E940  -> OnRightClick (+ TriggerSlot)
//   sub_68E3A0 (helper non nommé, appelé par ProcInput)              -> repris inline dans OnMouseUp
//   UI_ConsumableBar_Render       0x68E6E0  -> HORS PÉRIMÈTRE (rendu pixel, cf. bandeau en bas de fichier)
//
// ÉCART BINAIRE <-> MODULE (à lire avant d'utiliser ce fichier) :
// Le panneau d'origine (0x68E270) est un CATALOGUE FIXE de 28 cases (14 seulement
// renseignées par Init ; les 14 suivantes restent vides/réservées), qui n'affiche
// QUE des ITEM_INFO (aucune des 6 fonctions ne référence jamais SKILL_INFO) et sert
// à ouvrir un DRAG (Item_BeginDragTransaction, EA 0x5AFDF0) vers l'inventaire ou la
// vraie barre de raccourcis — ce n'est PAS lui-même un éditeur de raccourcis avec
// hotkeys/cooldowns. La mission demande néanmoins un moteur de décision générique
// « objets/compétences » opérant sur ts2::ui::QuickSlot (10 cases, déjà déclaré dans
// UI/GameHud.h, qui prévoit explicitement QuickSlotType::Skill et documente en
// commentaire les hotkeys DIK 0x02..0x0B) : c'est exactement le point d'accroche
// laissé en TODO par UI/GameHud.cpp::OnMouseDown (« déclencher l'usage du slot »).
// Ce module fournit donc CE moteur, fidèle aux gardes du binaire pour la partie
// Item (seule branche réellement présente dans les EA ci-dessus), et marque
// explicitement Unsupported/TODO la branche Skill : aucune des 6 EA assignées ne
// résout de compétence, et la vraie fonction d'activation de hotbar avec cooldown
// (distincte de UI_QuickSlot_AssignHotkey 0x5BDF00, qui ne fait QUE de la
// configuration d'auto-chasse, et de cQuickSlotWin_* 0x65F4F0-0x6627F0, qui gère la
// fenêtre d'édition mais pas — vérifié — l'exécution par hotkey) n'a pas été
// décompilée dans le cadre de cette tâche.
#pragma once
#include <array>
#include <cstdint>

#include "UI/GameHud.h"

namespace ts2::game {

using ConsumableSlots = std::array<ui::QuickSlot, ui::kQuickSlotCount>;

// Action décidée par TriggerSlot/OnClick/OnMouseUp/OnRightClick. Décrit
// l'intention SANS produire d'effet de bord (aucun envoi réseau, aucun appel
// Item_BeginDragTransaction/Item_DrawTooltip réel) : au code appelant de
// matérialiser la décision.
enum class ConsumableAction : uint8_t {
    None,           // rien à faire (slot vide, hors grille, ou garde en échec)
    Ignored,        // barre masquée -> évènement non consommé (EA 0x68E3CD/0x68E5AB/0x68E94C)
    Invalid,        // itemId assigné introuvable en base ITEM_INFO (EA 0x68E48F/0x68E491)
    BeginItemDrag,  // clic gauche sur slot Item plein -> Item_BeginDragTransaction (EA 0x68E46E..0x68E545)
    ShowTooltip,    // clic droit sur slot plein -> Item_DrawTooltip (EA 0x68E9B2)
    ArmCloseButton, // clic gauche sur le bouton fermer -> armement (EA 0x68E56C/0x68E588)
    ClosePanel,     // relâchement armé sur le bouton fermer -> ferme le panneau (EA 0x68E654/0x68E667 + sub_68E3A0)
    Unsupported,    // slot de type Skill : hors périmètre des EA RE, voir bandeau ci-dessus
};

struct ConsumableDecision {
    ConsumableAction action = ConsumableAction::None;
    int      slotIndex      = -1;
    uint32_t refId          = 0;     // itemId (ou skillId pour Unsupported)
    bool     consumed       = false; // valeur de retour int original (0/1) : évènement consommé ?

    // Renseigné seulement pour BeginItemDrag, branche typeCode==2 (EA 0x68E4A5) :
    bool promptQuantity = false; // byte_8013FE < 0 (EA 0x68E4B4) — signification exacte du flag TODO
    int  dragCount      = 0;     // 0 = quantité demandée (prompt), 99 = quantité fixe (EA 0x68E4E6/0x68E513/0x68E540)
    int  dragCursorX    = 0;     // a2-52 (EA 0x68E4E6), rempli seulement si promptQuantity
    int  dragCursorY    = 0;     // a3-72 (EA 0x68E4E6), rempli seulement si promptQuantity

    bool usable = false; // extension mission (pas dans les EA) : cf. IsSlotUsable/InventoryCount
};

// État complémentaire au tableau de QuickSlot : ce que l'objet ConsumableBar
// range en plus du catalogue (EA 0x68E270+).
struct ConsumableBarState {
    bool visible          = false; // *(this+2) — panneau affiché/actif
    bool closeButtonArmed = false; // *(this+3) — bouton fermer pressé (mousedown), en attente du mouseup
};

// EA 0x68E270 — initialise `slots` : vidage puis catalogue fixe d'items par
// défaut (potions/scrolls). Le binaire renseigne 14 des 28 cases (les 14
// suivantes restent à 0/vides) ; seules les ui::kQuickSlotCount (10) premières
// valeurs tiennent dans ce type. Les 4 dernières valeurs du catalogue d'origine
// (index 10..13 : itemId 1241, 1244, 1242, 1243 — EA 0x68E308/0x68E312/0x68E31C/
// 0x68E326) sont donc tronquées ici faute de place (TODO si kQuickSlotCount doit
// un jour couvrir les 14 entrées d'origine).
void InitConsumableBar(ConsumableSlots& slots);

// Constantes de grille reprises telles quelles de UI_ConsumableBar_HitTest
// (EA 0x68EA2D..0x68EA65) : 4 colonnes, pas de 52 px, case utile
// [x+11,x+61] x [y+37,y+87] relative à l'ancre (originX, originY).
inline constexpr int kGridColumns = 4;
inline constexpr int kGridStride  = 52;
inline constexpr int kGridCellX0  = 11, kGridCellX1 = 61;
inline constexpr int kGridCellY0  = 37, kGridCellY1 = 87;

// EA 0x68E9D0 — hit-test de la grille. `originX`/`originY` = ancre écran DÉJÀ
// résolue par le rendu (le calcul d'ancre via UI_ProjectSpriteToScreen 0x50F5D0 +
// Sprite2D_GetWidth 0x4D6CD0 est un recalcul dépendant du sprite de fond, HORS
// PÉRIMÈTRE logique — TODO EA 0x68E9F4-0x68EA2A, à faire porter par le rendu).
// Renvoie -1 si aucune case PLEINE n'est sous (mouseX, mouseY) — une case vide
// hit-testée renvoie aussi -1 (EA 0x68EAE3, garde `*(this+i+4) <= 0`).
int HitTestConsumableBar(const ConsumableSlots& slots, int originX, int originY,
                          int mouseX, int mouseY);

// EA 0x68E46E..0x68E545 (branche OnClick slot plein) et EA 0x68E9B2 (OnRightClick)
// — décide l'action pour le slot `index` SANS l'exécuter. `rightClick=false` =>
// logique OnClick (prépare un drag) ; `true` => logique OnRightClick (tooltip,
// pas de vérification d'existence dans le binaire). Slot Skill -> Unsupported
// (aucune EA de ce module ne résout de compétence, cf. bandeau en tête de fichier).
ConsumableDecision TriggerSlot(const ConsumableSlots& slots, int index, bool rightClick = false);

// EA 0x68E3C0 intégrale : garde de visibilité -> court-circuit HUD parent ->
// hit-test -> TriggerSlot, ou hit-test du bouton fermer si aucune case n'est
// touchée. `closeButtonHit` = résultat de Sprite2D_HitTest(unk_8F3798, ...)
// (EA 0x68E56C), calculé par le rendu (taille du sprite non disponible ici,
// HORS PÉRIMÈTRE). `parentHudConsumed` reproduit le court-circuit
// cGameHud_OnMouseDown 0x62B080 (EA 0x68E433), sous-système non implémenté ici.
ConsumableDecision OnClick(ConsumableBarState& state, const ConsumableSlots& slots,
                            int originX, int originY, int mouseX, int mouseY,
                            bool closeButtonHit, bool parentHudConsumed = false);

// EA 0x68E5A0 intégrale (relâchement souris). `closeButtonHit` = même hit-test
// que OnClick mais évalué à la position de relâchement (EA 0x68E654).
// `parentHudConsumed` reproduit cGameHud_OnMouseUp 0x62DFA0 (EA 0x68E611).
ConsumableDecision OnMouseUp(ConsumableBarState& state, bool closeButtonHit,
                              bool parentHudConsumed = false);

// EA 0x68E940 intégrale (clic droit -> tooltip). `tooltipDispatchConsumed`
// reproduit cGameHud_DrawTooltipDispatch 0x64EA30 (EA 0x68E963).
ConsumableDecision OnRightClick(const ConsumableBarState& state, const ConsumableSlots& slots,
                                 int originX, int originY, int mouseX, int mouseY,
                                 bool tooltipDispatchConsumed = false);

// ---------------------------------------------------------------------------
// Extensions demandées par la mission (« désactivation si objet insuffisant/
// compétence indisponible ») — ABSENTES des 6 EA RE (le catalogue d'origine
// n'interroge jamais l'inventaire possédé, seulement la table statique
// ITEM_INFO). Implémentées ici via game::g_World pour rendre TriggerSlot
// exploitable par un vrai hotbar piloté par l'inventaire du joueur.
// ---------------------------------------------------------------------------

// Quantité possédée de `itemId` dans l'inventaire du joueur local (somme des
// cellules game::g_Client.inv dont itemId correspond ; champ `flag` = compteur,
// cf. Game/ClientRuntime.h InventoryState::Set — modèle source de vérité, PAS
// game::g_World.self.inventory, retiré lors de la réconciliation des deux modèles
// d'inventaire concurrents, mission « inventaire », 2026-07-14).
uint32_t InventoryCount(uint32_t itemId);

// Vrai si le slot est déclenchable maintenant : Item -> connu en base
// ITEM_INFO ET possédé (>=1 exemplaire) ; Skill -> toujours false ici
// (TODO : nécessite Game/SkillSystem.h, hors contrat de headers de ce module,
// + la vraie fonction de cooldown/hotkey non localisée, cf. bandeau en tête).
// SANS APPELANT depuis W9 (2026-07-16) : son unique consommateur teintait en rouge
// les cases « objet manquant » de la barre HUD — un effet INVENTÉ (le binaire ne
// dessine aucun rectangle sur cette barre, cf. UI/ConsumableBarWindow.h), retiré
// avec le reste du rendu non fidèle. Conservé comme point d'extension déclaré.
bool IsSlotUsable(const ConsumableSlots& slots, int index);

// Déclenchement par touche numérique : mapping DIK_1..DIK_9 = 0x02..0x0A,
// DIK_0 = 0x0B -> index de slot 0..9 (convention déjà documentée par
// UI/GameHud.h lignes 27-30 et Input/InputSystem.h ; non inclus ici pour rester
// autonome). Aucune des 6 EA RE de ce module ne lit directement le clavier —
// ce mapping est repris de la convention existante, PAS traduit d'une EA du
// panneau ConsumableBar. Renvoie {None} si `dikScanCode` est hors plage.
ConsumableDecision TriggerSlotByHotkey(const ConsumableSlots& slots, uint8_t dikScanCode);

} // namespace ts2::game

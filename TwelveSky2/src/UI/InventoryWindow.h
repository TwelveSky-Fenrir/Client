// UI/InventoryWindow.h — Fenêtre « personnage » : inventaire (grille 8x8 x 2 pages
// max = 128 cases) + équipement (13 slots).
//
// Sous-ensemble inventaire/équipement de la classe cGameHud (singleton
// dword_1839568, onglet this[226]==1) — VÉRIFIÉ par décompilation (2026-07-14) :
// équipement (cGameHud_EquipSlotAtFilled 0x64EFC0) et sac (cGameHud_InvCellAt
// 0x64F9F0) sont dispatchés dans LA MÊME branche `case 1` de cGameHud_OnMouseDown
// 0x62B080 et dessinés par le même sous-ensemble de cGameHud_Render — UNE SEULE
// fenêtre dans le binaire, jamais deux fenêtres séparées. Cette classe respecte
// cette organisation (une seule InventoryWindow gère les deux). Réécriture C++
// PROPRE par-dessus les briques Gfx/Game/Asset. Réf. désassemblage (serveur idaTs2,
// voir Docs/TS2_CLIENT_SHELL.md §2.3) :
//   - cGameHud_InitLayout        0x62A5B0  table de rects des 13 slots (relatifs à base)
//   - cGameHud_ResetUiState      0x62AFB0  ouverture (this[175]=1, onglet 1)
//   - cGameHud_Hide              0x62B050  fermeture (this[175]=0)
//   - cGameHud_OnMouseDown       0x62B080  prise d'objet / onglets
//   - cGameHud_OnMouseUp         0x62DFA0  pose / actions boutons
//   - cGameHud_Render            0x64A900  rendu
//   - cGameHud_EquipSlotAtFilled 0x64EFC0  hit-test slots d'équipement occupés
//   - cGameHud_InvCellAt         0x64F9F0  hit-test grille 8x8 -> cellule d'objet
//   - Item_BeginDragTransaction  0x5AFDF0  démarre la prise (g_DragCtx 0x1822380)
//   - UI_ProjectSpriteToScreen   0x50F5D0  ancrage coords de référence -> écran
//
// Modèle drag&drop = « clic pour prendre / reclic pour poser » (PAS press-drag-release,
// confirmé par Item_BeginDragTransaction 0x5AFDF0 — CONSERVÉ tel quel ici, ne pas
// réécrire en press-hold-release qui trahirait la fidélité au binaire).
//
// Réseau (mission « drag-and-drop d'inventaire ») : Bind(NetClient*) attache
// (optionnellement) la session réseau, pattern identique à UI/ChatWindow.cpp
// et UI/WarehouseWindow.h (net_ nullable, no-op tant que non lié). RECHERCHE
// MENÉE dans Net/SendPackets.h (grep "swap"/"move"/"item" sur les 234 noms de
// builders) : AUCUN builder sortant ne porte un nom explicite de rôle — tous
// sont nommés par opcode brut (Net_SendOpNN / Net_SendVaultReq_NNN), pas de
// candidat "évident". Recoupement Docs/TS2_PROTOCOL_SPEC.md :
//   - Résultats SERVEUR confirmés pour le déplacement d'objet : 0x1e/0x1f
//     Pkt_ItemSwapResultA/B, 0x92 Net_OnItemMoveResult, 0x78 Net_OnEquipSlotUpdate.
//   - AUCUN appelant désassemblé ne relie ces résultats à un builder sortant
//     précis (note du doc, section [CS b06] : opcodes Vault 207..228 sont le
//     domaine ENTREPÔT — cf. WarehouseWindow — "exact per-field semantics are
//     not resolvable from the builders alone (callers needed)").
// REPLI PROPRE (voir NotifyServerItemMove() dans le .cpp) : pas d'opcode deviné
// (règle du projet : IDA = vérité unique) ; le déplacement reste 100% LOCAL,
// avec un point d'accroche explicite et documenté pour la suite.
//
// MODÈLE DE DONNÉES (réconciliation, mission « inventaire », 2026-07-14) : cette
// fenêtre lisait/écrivait AUPARAVANT game::g_World.self.inventory (vector<InvCell>,
// coordonnées x/y libres, « modèle simplifié » — voir historique git), un second
// modèle d'inventaire SÉPARÉ de game::g_Client.inv (grille row/col fixe) que TOUS
// les handlers réseau déjà câblés écrivent (Net/GameHandlers_InvCells.cpp,
// Net/ItemActionDispatch.cpp, Net/GameHandlers_VendorTrade.cpp, ... 20 fichiers).
// VERDICT (confirmé par désassemblage, cf. commentaire de game::InventoryState dans
// Game/ClientRuntime.h) : game::g_Client.inv EST le modèle fidèle — adressage
// [384*row + 6*col] (Pkt_ItemUpgradeResult 0x488DE0) / [(row%100)*0x600 +
// (col%100)*0x18] (Pkt_ItemActionDispatch 0x46A320), les deux réduisant à
// InventoryState::At(row,col) avec kCols=64. game::g_World.self.inventory a été
// SUPPRIMÉ ; cette fenêtre a été migrée pour lire/écrire game::g_Client.inv
// directement, avec `bagPage_` comme `row` (0 ou 1, cf. kMaxBagPages) et un slot
// `col` (0..63) dérivé de la position visuelle (gridX,gridY) de la cellule
// ANCRE — convention déjà établie indépendamment par Game/AutoPlaySystem.cpp
// (`g_Client.inv.cells[page*InventoryState::kCols + col]`).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Core/Types.h"
#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h" // game::g_Client.inv (InventoryState) : modèle source de vérité, cf. bandeau ci-dessus

namespace ts2::net { struct NetClient; }

namespace ts2::ui {

// Source d'un glisser-déposer (miroir de g_DragCtx.srcType, g_DragCtx 0x1822380 +0x10).
enum class DragSource : int {
    None   = 0,
    Bag    = 1,   // sac         (g_InvMain   0x16732B0)
    Equip  = 2,   // équipement  (g_EquipMain 0x16731D8)
    Quiver = 6,   // carquois    (g_QuiverMain 0x1673EB4)
};

// Contexte « clic pour prendre / reclic pour poser » — miroir partiel de g_DragCtx.
struct DragContext {
    bool       active      = false;             // +0x08 active
    DragSource srcType     = DragSource::None;  // +0x10 srcType
    int        srcPage     = 0;                 // +0x14 srcPage
    int        srcSlot     = -1;                // +0x18 srcSlot
    uint32_t   itemId      = 0;                 // +0x1C itemId
    int        count       = 0;                 // +0x28 count
    int        grabOffsetX = 0;                 // +0x44 grabOffsetX
    int        grabOffsetY = 0;                 // +0x48 grabOffsetY
    void reset() { *this = DragContext{}; }
};

// Sous-onglet équipement (cGameHud invSubTab this[227] / +0x38C).
enum class EquipSubTab : int {
    EquipPage1 = 1, // slots 0..8  (9 slots)
    EquipPage2 = 2, // slots 9..12 (4 slots)
    Quiver     = 3, // carquois (non dessiné ici)
};

class InventoryWindow {
public:
    // Résolveur itemId -> chemin de fichier .IMG de l'icône (chaîne vide si inconnu).
    // Câblé par défaut dans Init() vers ResolveItemIconPath (InventoryWindow.cpp) :
    // "G03_GDATA\D01_GIMAGE2D\002\002_%05u.IMG" formaté avec ITEM_INFO(itemId).IconID
    // (game::ItemInfo::iconId, champ +192, SÉPARÉ de itemId) — CONFIRMÉ par désassemblage,
    // cf. Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md et commentaire détaillé dans InventoryWindow.cpp.
    // Remplaçable via SetIconResolver() avant/après Init().
    using IconPathResolver = std::string (*)(uint32_t itemId);

    // Prend le device du renderer + une police (optionnelle, peut être nullptr).
    bool Init(gfx::Renderer& renderer, gfx::Font* font);
    void Shutdown();

    // Autour d'un Reset() de device D3D9 (pattern UI/GameHud.h::OnDeviceLost/Reset) :
    // sprite_ est un ID3DXSprite propre à cette fenêtre (pas le lot partagé de
    // UIManager) — il DOIT être libéré avant Reset() et reconstruit après, sous
    // peine de plantage/état invalide (D3DERR_DEVICELOST). background_/ownIconCache_/
    // sharedIconCache_/whiteTex_ sont tous D3DPOOL_MANAGED : restaurés automatiquement, rien à faire.
    void OnDeviceLost();
    void OnDeviceReset();

    // Attache la session réseau (nullable — cf. commentaire de tête de fichier
    // et NotifyServerItemMove() dans le .cpp). Pattern identique à
    // UI/ChatWindow.cpp::Bind / UI/WarehouseWindow.h::Bind.
    void Bind(net::NetClient* nc) { net_ = nc; }

    void SetIconResolver(IconPathResolver r) { iconResolver_ = r; }

    // Cache GPU d'icônes PARTAGÉ (mutualisation mémoire, cf. bandeau de tête de
    // Gfx/IconTextureCache.h) : injecté par UI/GameWindows.cpp (même instance que
    // WarehouseWindow/EnchantWindow/VendorShopWindow) pour qu'une icône commune à
    // plusieurs fenêtres ne soit chargée/uploadée qu'UNE seule fois en VRAM.
    // nullptr (repli) => utilise ownIconCache_ ci-dessous, propre à cette fenêtre
    // (jamais le cas en production, seulement si cette fenêtre est construite hors
    // GameWindows, ex. test unitaire isolé).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }
    // Fond du panneau (sprite HUD #299 dans l'original) ; recentre l'ancrage.
    bool SetBackgroundImage(const std::string& imgPath);
    // Dimensions écran courantes -> recalcule la base (UI_ProjectSpriteToScreen).
    void SetScreenSize(int width, int height);
    // Position curseur à jour (pour dessiner l'objet en cours de glissement).
    void SetCursorPos(int x, int y) { cursorX_ = x; cursorY_ = y; }

    // Cycle de vie (cGameHud_ResetUiState 0x62AFB0 / cGameHud_Hide 0x62B050).
    void Open();
    void Close();
    void Toggle();
    bool IsOpen() const { return visible_; }

    void SetEquipSubTab(EquipSubTab t) { equipSubTab_ = t; }
    // cGameHud_OnMouseDown case 1 (bouton unk_93F88C 0x62bc2b) : la page 1 n'est
    // proposee que si g_Inv_ExtraPageCount >= 1. Lu directement via
    // game::g_Client.VarGet(0x16732A8) — PAS via un champ SelfState dédié (un tel
    // champ existait brièvement puis a été retiré : rien ne l'écrivait, Net/
    // GameVarDispatch.cpp case 88 alimente exclusivement l'échappatoire Var(), même
    // source déjà utilisée par Game/QuestSystem.cpp — un second champ non écrit
    // aurait été une duplication silencieuse de plus). Sinon le clic est refusé
    // silencieusement ici (le binaire affiche StrTable005_Get(156), non reproduit —
    // hors périmètre HUD système). Voir kMaxBagPages ci-dessous : le binaire
    // n'expose jamais de page > 1.
    void SetBagPage(int page) {
        if (page != 0 && game::g_Client.VarGet(0x16732A8) < 1) return;
        bagPage_ = (page != 0) ? 1 : 0;
    }

    // Rendu (sous-ensemble inventaire/équipement de cGameHud_Render 0x64A900).
    void Render();

    // Événements souris. Retour true => consommé (règle « premier consommateur gagne »).
    bool OnMouseDown(int mouseX, int mouseY); // cGameHud_OnMouseDown 0x62B080
    bool OnMouseUp(int mouseX, int mouseY);   // cGameHud_OnMouseUp   0x62DFA0

    const DragContext& Drag() const { return drag_; }

private:
    struct SlotRect { int l, t, r, b; };
    struct TextItem { int x, y; std::string text; D3DCOLOR color; };

    // --- Géométrie (fidèle au désassemblage) ---
    void     RecomputeLayout();                          // base via UI_ProjectSpriteToScreen
    SlotRect EquipSlotRect(int slot) const;              // this[4*slot+2..+5]
    int      EquipSlotRectAt(int mx, int my) const;      // slot sous le curseur (rempli ou non)
    int      EquipSlotAt(int mx, int my) const;          // cGameHud_EquipSlotAtFilled 0x64EFC0
    bool     GridCellAt(int mx, int my, int& col, int& row) const; // 8x8 (0x64F9F0)
    // Slot occupé (0..63) sous le curseur dans game::g_Client.inv, page bagPage_ — PAS
    // un index de vecteur (cf. bandeau de tête de fichier : modèle unique g_Client.inv).
    int      InvCellAt(int mx, int my) const;
    static int ItemGridSize(uint32_t itemId);            // 1x1 (type 2/7/11) sinon 2x2
    // Slot (0..63) d'ancrage d'une cellule dans une page de game::g_Client.inv, dérivé
    // de sa position visuelle 8x8 — convention déjà établie indépendamment par
    // Game/AutoPlaySystem.cpp (page*InventoryState::kCols + col). PAS un offset
    // d'origine (le vrai g_PendingMove_SrcRow0/dword_1822EF0 est assigné par le
    // serveur) : convention LOCALE cohérente puisque cette fenêtre ne dialogue pas
    // encore réellement avec le réseau (cf. NotifyServerItemMove, no-op).
    static uint32_t StorageCol(uint32_t gridX, uint32_t gridY) {
        return gridY * static_cast<uint32_t>(kGridCols) + gridX;
    }

    // --- Rendu ---
    void               DrawItemIcon(uint32_t itemId, int x, int y, int wPx, int hPx, int count);
    gfx::GpuTexture*   GetIconTex(uint32_t itemId);      // lazy-load + cache (partagé, cf. SetIconCache)
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    // --- Drag&drop ---
    bool BeginPickup(int mx, int my);   // prise (retire de la source)
    bool PlaceDrag(int mx, int my);     // pose / échange sur la cible
    void CancelDrag();                  // retour à la source
    uint32_t DragColor() const;
    uint32_t DragDurability() const;

    // Point d'accroche réseau — repli propre documenté (cf. commentaire de tête
    // de fichier) : aucun opcode confirmé pour un déplacement bac/équipement
    // ordinaire (contrairement à l'entrepôt, cf. UI/WarehouseWindow.h). No-op
    // aujourd'hui ; à compléter le jour où un builder sera identifié par un
    // appelant désassemblé (net_ est déjà prêt via Bind()).
    void NotifyServerItemMove(const DragContext& ctx) const;

    bool PointInPanel(int mx, int my) const;

    // Rectangle plein uni (même technique que UI/UIManager.cpp::FillRect :
    // texture blanche 1x1 étirée + modulée par `color`). Doit être appelé À
    // L'INTÉRIEUR du batch sprite_ (entre Begin/End). Utilisé par le fond neutre
    // sous les icônes d'objet (cf. DrawItemIcon) — PLUS pour griser la case
    // source d'un glisser : cf. commentaire de Render() (CORRIGÉ par
    // désassemblage, 2026-07-14 — le binaire ne grise pas la source, il la vide).
    void FillRect(int x, int y, int w, int h, D3DCOLOR color);

    IDirect3DDevice9*  device_  = nullptr;
    gfx::Font*         font_    = nullptr;
    gfx::SpriteBatch   sprite_;
    gfx::GpuTexture    background_;
    // Cache d'icônes : voir SetIconCache()/ActiveIconCache() ci-dessus. ownIconCache_
    // n'est qu'un repli (jamais utilisé quand cette fenêtre est possédée par
    // UI::GameWindows, qui injecte systématiquement un cache PARTAGÉ à Init()).
    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;
    IconPathResolver   iconResolver_ = nullptr;
    IDirect3DTexture9* whiteTex_ = nullptr; // texture blanche 1x1 (utilitaire FillRect ci-dessus)
    net::NetClient*    net_      = nullptr; // session réseau optionnelle (cf. Bind())

    bool        visible_     = false;                    // this[175] / +0x2BC
    int         activeTab_   = 1;                        // this[226] / +0x388 (1=inv/équip)
    EquipSubTab equipSubTab_ = EquipSubTab::EquipPage1;  // this[227] / +0x38C
    int         bagPage_     = 0;                        // this[228] / +0x390

    int screenW_ = ts2::kRefWidth;
    int screenH_ = ts2::kRefHeight;
    int baseX_ = 0, baseY_ = 0;      // this[0]/this[1] (origine écran du panneau)
    int bgHalfW_ = 0, bgHalfH_ = 0;  // demi-dimensions du fond (recentrage)
    int cursorX_ = 0, cursorY_ = 0;

    DragContext      drag_;
    game::InvCell    dragBagCell_{};   // sauvegarde source (sac) pour annulation/échange
    game::EquipSlot  dragEquipCell_{}; // sauvegarde source (équip) pour annulation/échange

    std::vector<TextItem> pendingText_; // passe texte différée (hors batch sprite)

    // --- Constantes de géométrie relevées dans le désassemblage ---
    // CAPACITÉ RÉELLE (vérifiée par décompilation de cGameHud_InvCellAt 0x64F9F0 et
    // Inv_FindFreeCellForItem 0x650FA0, 2026-07-14) : grille = 8 colonnes x 8 lignes
    // = 64 cases PAR PAGE (boucles `for(i<8) for(j<8)` / `for(k<64)` confirmées dans
    // les deux fonctions), PAS une grille plus grande. Le sac a AU MAXIMUM 2 pages
    // (kMaxBagPages) : page 0 toujours active, page 1 conditionnelle à
    // game::g_Client.VarGet(0x16732A8) >= 1 (g_Inv_ExtraPageCount, cf. SetBagPage
    // ci-dessus) — cGameHud_OnMouseDown 0x62B080 case 1 n'a que 2 boutons de page
    // (unk_93F7F8 -> page 0, unk_93F88C -> page 1, ce dernier gated par
    // g_Inv_ExtraPageCount) : le binaire n'expose jamais de 3e page.
    // Capacité totale sac = kMaxBagPages * kGridCols * kGridRows = 128 cases max.
    static constexpr int kMaxBagPages = 2;
    static constexpr int kRefX     = 764; // arg a3 de UI_ProjectSpriteToScreen (cGameHud)
    static constexpr int kRefY     = 182; // arg a4
    static constexpr int kGridCols = 8;
    static constexpr int kGridRows = 8;
    static constexpr int kCellStep = 26;  // pas de cellule (base+26*i+34 / base+26*j+193)
    static constexpr int kCellOffX = 34;  // décalage X de la 1re colonne
    static constexpr int kCellOffY = 193; // décalage Y de la 1re rangée
    static constexpr int kCellSize = 25;  // 59-34 == 218-193

    static constexpr D3DCOLOR kLabelColor    = 0xFFFFEE66u; // repli id d'objet (jaune pâle)
    static constexpr D3DCOLOR kCountColor    = 0xFFFFFFFFu; // compteur de pile (blanc)
};

} // namespace ts2::ui

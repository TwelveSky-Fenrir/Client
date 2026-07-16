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
// ===========================================================================
// RÉSEAU — CORRECTIF W6 (l'ancien bandeau de ce fichier était FAUX, prouvé)
// ===========================================================================
// L'ancien texte affirmait « AUCUN builder sortant ... le déplacement reste 100%
// LOCAL ... aucun opcode confirmé ». Les DEUX moitiés sont réfutées par IDA :
//
//  1. Le handler de POSE n'est PAS cGameHud_OnMouseUp 0x62DFA0 (ancré à tort ici) :
//     ses call-sites ne contiennent que du skill/stat, ZÉRO émission d'objet. Le vrai
//     handler est UI_MainInventory_OnLButtonUp 0x5B20B0 (0xBDDB o), un switch géant
//     `switch(g_DragCtx+0x10 /*srcType*/)` (this = g_DragCtx 0x1822380).
//  2. Les builders EXISTENT tous déjà dans Net/SendPackets.h (Net_SendVaultReq_*,
//     56 sous-codes de l'opcode réseau 0x13/Op19). Vérifié 1:1 par xrefs_to sur
//     chaque sous-code : chaque EA ci-dessous a été relu dans l'IDB.
//
// Émissions de CETTE fenêtre (sac + équipement ; le carquois/la barre rapide sont
// d'autres widgets) — layout universel des 7 champs, tous promus sur 4 o LE :
//   (srcPage, srcSlot, amount, dstPage, dstSlot, dstGridX, dstGridY)
//
//   Sac -> sac (déplacer/fusionner)  VaultReq_208  @0x5B22FC
//        args (+0x14, +0x18, +0x28, dstPage, dstSlot, dstGridX, dstGridY)
//   Sac -> équipement (ÉQUIPER)      VaultReq_210  @0x5B2555
//        args (+0x14, +0x18, +0x28, 0, equipSlot, 0, 0)
//   Équipement -> sac (DÉSÉQUIPER)   VaultReq_213  @0x5BA28C
//        args (0, +0x18, +0x20, dstPage, dstSlot, dstGridX, dstGridY)
//
// ATTENTION (+0x20 vs +0x28) : le 3e champ n'est PAS le même selon la source, car
// Item_BeginDragTransaction 0x5AFDF0 range ses arguments par TYPE (cf. DragContext) :
//   - prise SAC   (0x62B5FB) : a6=gridX -> +0x20 ; a8=count      -> +0x28 (208 lit +0x28)
//   - prise ÉQUIP (0x62B199) : a6=durability -> +0x20 ; a8=serial -> +0x28 (213 lit +0x20)
//
// MODÈLE OPTIMISTE — NON : le binaire n'écrit RIEN localement à la pose.
// cGameHud_PlaceItemIntoBag 0x650470 (nom trompeur) est une REQUÊTE PURE : elle ne
// lit que g_InvMain/g_InvGrid_* et n'écrit QUE ses out-params. La pose se borne donc à
// (a) calculer la destination, (b) émettre, (c) poser g_DragCtx+0x0C = 1 (ack en
// attente) SANS appeler Item_DragState_Clear -> l'objet RESTE sur le curseur jusqu'à
// la réponse serveur. La case n'est vidée qu'à la PRISE (Inv_RemoveItemQuantity).
// -> PlaceDrag() ci-dessous n'écrit donc AUCUNE cellule de destination.
//
// ÉCHANGE : le binaire n'échange JAMAIS. Sur case occupée, 0x650470 ne réussit que
// pour une FUSION DE PILE (type==2 && même itemId && somme <= 99) ; sinon *a6 = -1
// -> refus + restauration. Sur slot d'équipement occupé, cGameHud_EquipSlotAtEmpty
// 0x64F140 renvoie -1 (`if (g_EquipMain[4*i] >= 1) return -1;`) -> pas de cible.
// L'ancien « échange » de PlaceDrag était une INVENTION : supprimé.
//
// PAS DE Bind(NetClient*) : le binaire adresse g_NetClient 0x8156A0 en GLOBAL (les
// builders ne reçoivent jamais de socket). L'ancien couple Bind()/net_ était du CODE
// MORT prouvé — `inventory_.Bind(...)` n'existait NULLE PART dans la composition
// (seul skillTree_.Bind(), UI/GameWindows.cpp:72) -> net_ toujours nul -> le
// `if (!net_) return` de NotifyServerItemMove() bloquait tout envoi. On utilise
// net::GlobalNetClient() (Net/NetClient.h:67-68), renseigné par ConnectGameServer.
// Même correctif que UI/GuildWindow.h (W6).
// ===========================================================================
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

namespace ts2::ui {

// Source d'un glisser-déposer (miroir de g_DragCtx.srcType, g_DragCtx 0x1822380 +0x10).
enum class DragSource : int {
    None   = 0,
    Bag    = 1,   // sac         (g_InvMain   0x16732B0)
    Equip  = 2,   // équipement  (g_EquipMain 0x16731D8)
    Quiver = 6,   // carquois    (g_QuiverMain 0x1673EB4)
};

// Contexte « clic pour prendre / reclic pour poser » — miroir partiel de g_DragCtx
// 0x1822380. Offsets PROUVÉS par décompilation de Item_BeginDragTransaction 0x5AFDF0
// (`*(this+N) = aM`, this typé int* -> offset = 4*N).
//
// ÉCART STRUCTUREL ASSUMÉ (remonté au rapport W6) : dans le binaire g_DragCtx est un
// GLOBAL partagé par TOUS les widgets de pose (inventaire, entrepôt, carquois, barre
// rapide, fusion) ET par les handlers réseau entrants, qui appellent
// Item_DragState_Clear 0x5B02D0 à l'arrivée de l'ack. Ici le contexte est un MEMBRE de
// InventoryWindow : Net/GameHandlers_InvCells.cpp ne peut donc pas le remettre à zéro
// (il ne remet que net::g_GmCmdCooldownLatch). Conséquence fidèle mais incomplète :
// après une pose émise, l'objet reste collé au curseur (pendingAck) jusqu'à ce qu'un
// futur front recentralise g_DragCtx. Ne PAS « corriger » par un reset local optimiste :
// le binaire ne le fait pas (cf. bandeau de tête).
struct DragContext {
    bool       active      = false;             // +0x08 active   (garde d'entrée de 0x5AFDF0)
    bool       pendingAck  = false;             // +0x0C ack en attente : mis à 0 à la PRISE
                                                //       (`*(this+3) = 0` @0x5AFDF0), à 1 après
                                                //       CHAQUE émission (ex. @0x5B2301, 0x5BA297)
    DragSource srcType     = DragSource::None;  // +0x10 srcType (a2)
    int        srcPage     = 0;                 // +0x14 srcPage (a3)
    int        srcSlot     = -1;                // +0x18 srcSlot (a4)
    uint32_t   itemId      = 0;                 // +0x1C itemId  (a5)
    // +0x20 (a6) : champ DÉPENDANT DU TYPE DE SOURCE (cf. bandeau de tête).
    //   srcType 1 (sac)   -> gridX      (0x62B5FB)
    //   srcType 2 (équip) -> durability (0x62B199) — c'est CE champ que lit VaultReq_213
    int        aux20       = 0;
    int        count       = 0;                 // +0x28 count (a8) — lu par VaultReq_208/210/212
    int        grabOffsetX = 0;                 // +0x44 grabOffsetX (a12)
    int        grabOffsetY = 0;                 // +0x48 grabOffsetY (a13)
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

    // PAS de Bind(net::NetClient*) : le binaire adresse g_NetClient 0x8156A0 en GLOBAL
    // (les 234 builders Net_Send* ne reçoivent jamais de socket). L'émission passe donc
    // par net::GlobalNetClient() (cf. bandeau de tête + helpers Emit*). L'ancien couple
    // Bind()/net_ était du CODE MORT prouvé (aucun `inventory_.Bind(...)` dans toute la
    // composition — seul skillTree_.Bind() existe, UI/GameWindows.cpp:72) : supprimé.

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
    // Game/AutoPlaySystem.cpp (page*InventoryState::kCols + col). Sert AUSSI de dstSlot
    // pour les émissions sac->sac / équip->sac (cf. EmitMoveBagToBag/EmitUnequipToBag) :
    // le binaire calcule sa destination via cGameHud_PlaceItemIntoBag 0x650470 /
    // cGameHud_FindInvPlacement 0x64FCA0 (non entièrement désassemblées) ; ce mapping
    // cursor-cell -> slot en est l'approximation cohérente côté client (cf. rapport).
    static uint32_t StorageCol(uint32_t gridX, uint32_t gridY) {
        return gridY * static_cast<uint32_t>(kGridCols) + gridX;
    }

    // --- Rendu ---
    void               DrawItemIcon(uint32_t itemId, int x, int y, int wPx, int hPx, int count);
    gfx::GpuTexture*   GetIconTex(uint32_t itemId);      // lazy-load + cache (partagé, cf. SetIconCache)
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    // --- Drag&drop ---
    bool BeginPickup(int mx, int my);   // prise (retire de la source, Item_BeginDragTransaction 0x5AFDF0)
    bool PlaceDrag(int mx, int my);     // pose : émet le VaultReq idoine (cf. helpers ci-dessous)
    void CancelDrag();                  // retour à la source (= Inv_AddItemQuantity + Item_DragState_Clear)
    uint32_t DragColor() const;
    uint32_t DragDurability() const;

    // --- Émission réseau — miroir de UI_MainInventory_OnLButtonUp 0x5B20B0
    // (switch(g_DragCtx+0x10 /*srcType*/)). Chaque helper adresse g_NetClient
    // 0x8156A0 via net::GlobalNetClient() (pattern GLOBAL du binaire, cf. bandeau
    // de tête) : PAS d'injection de socket. Ils reproduisent les gardes prouvées,
    // émettent, posent pendingAck, MAIS n'écrivent AUCUNE cellule de destination
    // (le binaire émet puis attend le retour serveur). Renvoient true = clic consommé.
    bool EmitMoveBagToBag(int col, int row, int occ);   // VaultReq_208 @0x5B22FC (sac->sac)
    bool EmitEquipFromBag(int equipSlot);               // VaultReq_210 @0x5B2555 (sac->équip)
    bool EmitUnequipToBag(int col, int row, int occ);   // VaultReq_213 @0x5BA28C (équip->sac)
    // Garde universelle (0x5B2297 / 0x5B23E7 / 0x5BA312) : morph en cours OU requête
    // déjà en vol -> true (l'appelant refuse : restaure + clôt le drag).
    bool EmissionBlockedByMorphOrLatch() const;
    // Épilogue commun post-émission (0x5B2301.. / 0x5BA291..) : pendingAck, verrou
    // anti-spam g_GmCmdCooldownLatch, horodatage flt_1675B0C, drapeau dirty.
    void MarkEmissionPending();

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

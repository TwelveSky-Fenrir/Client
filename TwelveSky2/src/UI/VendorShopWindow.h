// UI/VendorShopWindow.h — fenêtre MARCHAND (boutique NPC) du client TS2.
//
// Rétro-ingénierie DIRECTE de l'état catalogue marchand, qui n'est PAS regroupé
// dans une struct dédiée côté binaire (contrairement à l'entrepôt/WarehouseSystem) :
// c'est une longue traîne de globals scalaires peuplés entrée par entrée par le
// handler entrant Pkt_VendorItemEntry (opcode 0x25), voir
// Net/GameHandlers_VendorTrade.cpp (NE PAS éditer) :
//   dword_1826134            (Var 0x1826134) = nb d'entrées catalogue chargées
//   dword_1826128            (Var 0x1826128) = nb de pages (10 entrées/page)
//   dword_1826130            (Var 0x1826130) = sélection courante (-1 = aucune)
//   dword_182613C[idx]       (Var(0x182613C + 4*idx))  = itemId de l'entrée idx
//   dword_1834F84[3*idx..+8] (Var(0x1834F84 + 12*idx) + 4 + 8) = prix (3 composantes)
// On accède à ces globals via l'échappatoire g_Client.Var()/VarGet() de
// Game/ClientRuntime.h (déjà écrit, NE PAS éditer) — AUCUN champ propre n'est requis
// puisque le catalogue est déjà entièrement dans ces adresses.
//
// La fenêtre elle-même ne fait qu'AFFICHER/PAGINER ce catalogue et piloter la
// sélection LOCALE (Var(0x1826130)).
//
// ACHAT (mise à jour 2026-07-16, vague W6 — preuve IDA fraîche) : le vrai handler
// d'achat est UI_NpcShop_OnRDown_Buy 0x5e5000 (clic DROIT sur une case marchande),
// SEULE émission de la famille UI_NpcShop_* (Enter 0x5e43f0 / OnLDown 0x5e44a0 /
// OnLUp 0x5e4760 sont 100 % locales). Sa chaîne de gardes est désormais reproduite
// à l'identique dans HandleBuyClick (voir le .cpp), mais l'ÉMISSION reste bloquée :
// 4 des 7 champs du paquet (page/freeSlot/col/row) proviennent de
// Inventory_FindFreeGridSlot 0x54DDE0 + Inventory_FindFreePageSlot 0x54E1D0, qui
// n'ont aucun équivalent C++ (logique d'allocation d'inventaire relevant de Game/).
// Le transport lui-même est CONNU et DISPONIBLE (Net_SendPacket_Op19, sous-code 215,
// bloc 100 o) — cf. TODO [ancre 0x5e562a] dans le .cpp et le rapport de vague.
//
// Icône + nom d'une entrée : PAS reversés depuis le paquet réseau lui-même — le nom
// brut serveur (p.name, 13 o, VendorItemEntry 0x25) est un TODO(state) non stocké
// côté Net/GameHandlers_VendorTrade.cpp (unk_18270DC + 13*idx). On résout donc
// nom + icône localement à partir du SEUL champ fiable déjà capturé, l'itemId
// (dword_182613C[idx]), via la base ITEM_INFO déjà chargée (Game/GameDatabase.h) —
// même source que InventoryWindow (ItemInfo::name +4, ItemInfo::iconId +192).
// Chemin fichier icône identique au pattern InventoryWindow.cpp/ResolveItemIconPath :
// "G03_GDATA\D01_GIMAGE2D\002\002_%05u.IMG" formaté avec ItemInfo::iconId (PAS itemId).
//
// Règle du projet : ce fichier n'édite AUCUN header existant ; il inclut
// UI/UIManager.h et Game/ClientRuntime.h en lecture seule.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::ui {

class VendorShopWindow : public Dialog {
public:
    VendorShopWindow();

    // Ouverture : recale la page courante sur la sélection existante (si une
    // entrée est déjà sélectionnée par un handler réseau) et remet la quantité
    // d'achat à 1. Fermeture : annule la sélection en cours (Var(0x1826130)=-1)
    // pour ne rien laisser « en l'air » entre deux ouvertures, comme WarehouseWindow.
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Cache GPU d'icônes PARTAGÉ (mutualisation mémoire, cf. Gfx/IconTextureCache.h) :
    // injecté par UI/GameWindows.cpp, même instance que InventoryWindow/WarehouseWindow/
    // EnchantWindow. nullptr (repli) => ownIconCache_ locale (jamais le cas en production,
    // cf. InventoryWindow::SetIconCache).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

private:
    struct Rect { int x, y, w, h; };

    // --- Accès au catalogue (adresses d'origine, via g_Client.Var/VarGet) ---
    static constexpr uint32_t kAddrEntryCount = 0x1826134; // dword_1826134
    static constexpr uint32_t kAddrPageCount  = 0x1826128; // dword_1826128
    static constexpr uint32_t kAddrSelection  = 0x1826130; // dword_1826130
    static constexpr uint32_t kAddrItemIdBase = 0x182613C; // dword_182613C[idx]
    static constexpr uint32_t kAddrPriceBase  = 0x1834F84; // dword_1834F84[3*idx]
    static constexpr uint32_t kAddrVendorNpc  = 0x1837E6C; // dword_1837E6C (id/param vendeur, Pkt_VendorInventoryLoad)

    int  EntryCount() const;                 // Var(kAddrEntryCount), borné >=0
    int  PageCount() const;                  // Var(kAddrPageCount), borné >=1
    int  Selection() const;                  // Var(kAddrSelection) (-1 = aucune)
    void SetSelection(int idx);
    uint32_t ItemIdAt(int idx) const;         // Var(kAddrItemIdBase + 4*idx)
    uint32_t PriceAt(int idx, int component) const; // Var(kAddrPriceBase + 12*idx + 4*component)
    std::string ItemNameAt(int idx) const;    // ITEM_INFO(itemId).name, repli "#<id>" si base absente/slot inconnu

    // --- Icône (chargement paresseux + cache, pattern InventoryWindow::GetIconTex) ---
    gfx::GpuTexture* GetIconTex(const UiContext& ctx, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    // Repositionne (x_, y_) À CHAQUE FRAME depuis les dimensions écran RÉELLES
    // (ctx.screenW/H), au lieu du calcul figé une seule fois au constructeur (bug
    // corrigé le 2026-07-14 — cf. commentaire de tête du .cpp pour les preuves IDA :
    // UI_NpcShop_Draw/OnLDown/HitTestWindow reproduisent tous le même ancrage design
    // (764,182) via UI_ProjectSpriteToScreen 0x50f5d0). Même pattern que
    // AutoPlayWindow::RecomputeLayout / PlayerTradeWindow::Layout / OptionsWindow::Layout.
    void RecomputeAnchor(int screenW, int screenH);

    // --- Géométrie (coordonnées panneau, référence 1024x768) ---
    Rect PanelRect() const;
    Rect CloseButtonRect() const;
    Rect RowRect(int rowInPage) const;        // ligne 0..kRowsPerPage-1
    Rect PrevPageButtonRect() const;
    Rect NextPageButtonRect() const;
    Rect QtyMinusButtonRect() const;
    Rect QtyPlusButtonRect() const;
    Rect BuyButtonRect() const;
    bool RowAt(int mx, int my, int& outRowInPage) const;
    bool PointInPanel(int mx, int my) const;

    // --- Actions ---
    void HandleRowClick(int rowInPage);
    void HandleBuyClick();
    void ClampPage();

    // Refus d'achat : reproduit `Msg_AppendSystemLine(g_ChatManager,
    // StrTable005_Get(g_LangId, id), g_SysMsgColor)` — l'idiome de refus de
    // UI_NpcShop_OnRDown_Buy 0x5e5000 (11 sites, EA 0x5e5243..0x5e566c).
    // `strTableId` = id StrTable005 EXACT relevé au désassemblage.
    void Refuse(int strTableId);

    static constexpr int kRowsPerPage = 10;   // fidèle : "10 entrées/page"
    static constexpr int kGridPad     = 12;
    static constexpr int kHeaderH     = 26;
    static constexpr int kRowH        = 22;
    static constexpr int kRowGap      = 2;
    static constexpr int kCloseSize   = 18;
    static constexpr int kPageBtnW    = 24;
    static constexpr int kPageBtnH    = 20;
    static constexpr int kQtyBtnW     = 20;
    static constexpr int kQtyBtnH     = 20;
    static constexpr int kBuyBtnW     = 110;
    static constexpr int kBuyBtnH     = 26;
    static constexpr int kFooterH     = 84;
    // Décalages verticaux des 3 lignes du pied de fenêtre (relatifs à footerTop),
    // calculés une fois ici pour que géométrie et rendu restent synchronisés.
    static constexpr int kFooterRow1Y = 6;                                   // pagination
    static constexpr int kFooterRow2Y = kFooterRow1Y + kPageBtnH + 8;        // qté + Acheter
    static constexpr int kFooterRow3Y = kFooterRow2Y + kBuyBtnH + 8;         // statut/or

    static constexpr int kPanelW = 340;
    static constexpr int kPanelH = kHeaderH + kGridPad
        + kRowsPerPage * kRowH + (kRowsPerPage - 1) * kRowGap
        + kGridPad + kFooterH;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, cf. consigne de mission) ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u; // fond panneau
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u; // cadre
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u; // titre
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu; // texte
    static constexpr D3DCOLOR kColTextDim   = 0xFFAAAAAAu; // texte atténué
    static constexpr D3DCOLOR kColSelect    = 0xFF4060A0u; // survol/sélection
    static constexpr D3DCOLOR kColError     = 0xFFFF6060u; // erreur
    static constexpr D3DCOLOR kColSuccess   = 0xFF60FF60u; // succès
    static constexpr D3DCOLOR kColHeaderBg  = 0xFF2A2A34u; // bandeau titre
    static constexpr D3DCOLOR kColRowBg     = 0xFF34343Eu; // ligne (pair)
    static constexpr D3DCOLOR kColRowBgAlt  = 0xFF2C2C34u; // ligne (impair)
    static constexpr D3DCOLOR kColBtnBg     = 0xFF3A3A46u; // bouton actif
    static constexpr D3DCOLOR kColBtnBgOff  = 0xFF262629u; // bouton désactivé
    static constexpr D3DCOLOR kColIconFallback = 0xFF484858u; // repli icône (résolution/texture échouée)

    int         curPage_   = 1;   // page courante affichée (1-based), locale à la fenêtre
    int         qty_       = 1;   // quantité d'achat (1..99), locale à la fenêtre
    std::string statusText_;      // dernier statut d'action (achat), affiché en pied de fenêtre

    // Cache icônes PARTAGÉ (chemin de fichier -> texture GPU), lazy-load à la 1re
    // apparition d'un chemin ; mémorise aussi les échecs (texture invalide) pour ne pas
    // ré-essayer chaque frame, cf. InventoryWindow::GetIconTex / Gfx/IconTextureCache.h.
    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;
};

} // namespace ts2::ui

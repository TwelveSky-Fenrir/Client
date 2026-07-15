// UI/WarehouseWindow.h — fenêtre ENTREPÔT (Warehouse/Storage) du client TS2.
//
// Vue 5x5 sur game::g_Warehouse (Game/WarehouseSystem.h, déjà écrit). Le blob
// réseau de 1232 o (WarehouseGrid) est déjà décodé côté handlers Pkt_WarehouseOpen/
// Pkt_WarehouseUpdate ; cette fenêtre AFFICHE la grille 5x5 qui en résulte et
// pilote les actions de sélection/échange/retrait :
//   - clic sur une cellule non vide, aucune sélection en cours
//       -> WarehouseState::SelectPendingMove(row,col). RETOUR VISUEL — CORRIGÉ
//          par désassemblage (mission « retour visuel du glisser-déposer »,
//          2026-07-14, décompilation Item_BeginDragTransaction 0x5AFDF0 +
//          Inv_RemoveItemQuantity 0x5B0340 case 18/19/27/28 + UI_StorageWin_Draw
//          0x5D6610 + maybe_UI_QuickSlotBar_Render 0x5BE340) : l'ancienne
//          hypothèse « cellule source grisée » (kColDragSource/kColDragSourceOverlay)
//          était FAUSSE — le binaire retire l'objet de la grille source dès la
//          saisie (Inv_RemoveItemQuantity y met l'itemId à 0), donc
//          UI_StorageWin_Draw (qui ne dessine l'icône QUE si `champ >= 1`) ne
//          dessine RIEN à cet emplacement : la case source redevient une case
//          VIDE ORDINAIRE, sans teinte particulière. En contrepartie, l'objet
//          saisi est dessiné CENTRÉ SUR LE CURSEUR à chaque frame tant que le
//          glisser est actif (maybe_UI_QuickSlotBar_Render, dispatch sur
//          g_DragCtx, case 27/28 = type entrepôt), ce que l'implémentation
//          précédente ne faisait PAS du tout. Voir Render() dans le .cpp.
//   - reclic sur la MÊME cellule déjà sélectionnée
//       -> désélection (WarehouseState::CancelPendingMove)
//   - reclic sur une AUTRE cellule de la grille (vide ou occupée)
//       -> WarehouseState::SwapCells (échange local, « tri ») PUIS envoi réseau
//          RÉEL de la grille via SendGridCommit(kind=5), si Bind() a été appelé.
//   - clic sur le bouton « Retirer -> Sac » (actif seulement si une cellule est
//     sélectionnée) -> WarehouseState::CommitCellToInventory (dépose l'objet
//     dans l'inventaire général g_Client.inv, vide la cellule entrepôt) PUIS
//     SendGridCommit(kind=4).
//
// Réseau : Bind(NetClient*) attache (optionnellement) la session réseau, à
// l'image de UI/ChatWindow.cpp (net_ nullable, no-op tant que non lié — AUCUN
// crash, juste un déplacement local sans confirmation serveur). Le builder
// câblé est Net_SendPacket_Op31 (Net/SendPackets.h) : SEUL builder sortant dont
// la charge utile, `int8_t kind + 1232 o`, correspond EXACTEMENT à
// sizeof(WarehouseGrid) ; opcode sortant 0x1f, cf. Net/Opcodes.h « Op31 = 0x1f,
// // selecteur + blob de 1232 o ». kind=5 (tri, UI_StorageWin_Open case 5,
// EA 0x5d2c32) / kind=4 (retrait, case 4) d'après le layout documenté en tête
// de Game/WarehouseSystem.h.
// NB intégration : Bind() n'est appelé nulle part dans la composition actuelle
// (App/SceneManager, hors périmètre de cette mission — cf. consigne "ne pas
// toucher Scene/SceneManager.*"). UI/GameWindows.h expose déjà l'accesseur
// `WarehouseWindow& GameWindows::Warehouse()` : il suffira d'ajouter, côté
// SceneManager (là où windows_->Init(...) est appelé, avec net_ déjà membre),
// `windows_->Warehouse().Bind(&net_->Client());` pour finaliser le branchement
// live — un seul appel, aucune modification de signature nécessaire ici.
//
// Règle du projet : ce fichier n'édite AUCUN header existant ; il inclut
// UI/UIManager.h, Game/WarehouseSystem.h, Game/ClientRuntime.h et Net/NetClient.h
// (forward-déclaré seulement, cf. pattern ChatWindow) en lecture seule.
#pragma once
#include "UI/UIManager.h"
#include "Game/WarehouseSystem.h"
#include "Game/ClientRuntime.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::net { struct NetClient; }

namespace ts2::ui {

class WarehouseWindow : public Dialog {
public:
    WarehouseWindow();

    // Attache la session réseau (nullable — cf. commentaire de tête de fichier).
    // Pattern identique à UI/ChatWindow.cpp::Bind : pas d'inclusion lourde
    // Net/NetClient.h ici (forward-decl uniquement), le .cpp fait l'inclusion.
    void Bind(net::NetClient* nc) { net_ = nc; }

    // Cache GPU d'icônes PARTAGÉ (mutualisation mémoire, cf. Gfx/IconTextureCache.h) :
    // injecté par UI/GameWindows.cpp, même instance que InventoryWindow/EnchantWindow/
    // VendorShopWindow. nullptr (repli) => ownIconCache_ locale (jamais le cas en
    // production, cf. InventoryWindow::SetIconCache).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

    // Ouverture/fermeture (Dialog::Open/Close). L'ouverture n'affecte pas la
    // grille (déjà peuplée par les handlers réseau) ; la fermeture annule une
    // éventuelle sélection en attente pour ne rien laisser « en l'air ».
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    Rect PanelRect() const;
    Rect CloseButtonRect() const;
    Rect WithdrawButtonRect() const;
    Rect CellRect(int row, int col) const;
    bool CellAt(int mx, int my, int& outRow, int& outCol) const;
    bool PointInPanel(int mx, int my) const;

    // Recalcule x_/y_ (centrage) depuis les dimensions écran RÉELLES courantes —
    // appelé à CHAQUE frame par Render() (cf. bandeau de tête du .cpp : bug
    // "fenêtre figée à la position de conception" corrigé, mission fenêtres mal
    // ajustées). Même pattern que EnchantWindow::ComputeLayout / MsgBoxDialog::Layout,
    // conforme au contrat documenté par Dialog::x_/y_ ("position écran recentrée
    // chaque frame", cf. UI/UIManager.h).
    void RecomputeCenter(int screenW, int screenH);

    void HandleCellClick(int row, int col);
    void HandleWithdrawClick();

    // Envoie la grille entrepôt courante au serveur (Net_SendPacket_Op31, cf.
    // commentaire de tête de fichier). No-op silencieux si net_ n'est pas lié
    // (Bind() jamais appelé) — le déplacement reste alors purement local,
    // exactement comme avant le câblage réseau.
    void SendGridCommit(int8_t kind);

    static std::string CellLabel(const game::WarehouseItemCell& cell);

    // --- Icônes d'objet (même pattern que InventoryWindow : résolveur + cache paresseux
    // + repli sur la cellule colorée existante si la texture ne charge pas). Le device D3D9
    // n'est accessible qu'au rendu (ctx.renderer), d'où la prise du device en paramètre ici
    // plutôt qu'un device_ membre comme dans InventoryWindow (cette fenêtre n'a pas d'Init()
    // dédié — Dialog ne prévoit pas de device au moment de la construction).
    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;

    // --- Géométrie (dimensions panneau fixes, référence 1024x768 ; ORIGINE x_/y_ (héritée
    // de Dialog) recentrée chaque frame par RecomputeCenter() ci-dessus — PAS figée) ---
    static constexpr int kCellSize  = 48;
    static constexpr int kCellGap   = 4;
    static constexpr int kGridPad   = 12;
    static constexpr int kHeaderH   = 26;
    static constexpr int kFooterH   = 58;
    static constexpr int kCloseSize = 18;
    static constexpr int kBtnW      = 150;
    static constexpr int kBtnH      = 24;

    static constexpr int kPanelW = kGridPad * 2
        + game::WarehouseGrid::kCols * kCellSize
        + (game::WarehouseGrid::kCols - 1) * kCellGap;
    static constexpr int kPanelH = kHeaderH + kGridPad
        + game::WarehouseGrid::kRows * kCellSize
        + (game::WarehouseGrid::kRows - 1) * kCellGap
        + kGridPad + kFooterH;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, cf. consigne de mission) ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u; // fond panneau
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u; // cadre
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u; // titre
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu; // texte
    static constexpr D3DCOLOR kColTextDim   = 0xFFAAAAAAu; // texte atténué
    static constexpr D3DCOLOR kColSelect    = 0xFF4060A0u; // survol (cible potentielle)
    static constexpr D3DCOLOR kColError     = 0xFFFF6060u; // erreur
    static constexpr D3DCOLOR kColSuccess   = 0xFF60FF60u; // succès
    static constexpr D3DCOLOR kColHeaderBg  = 0xFF2A2A34u; // bandeau titre
    static constexpr D3DCOLOR kColCellBg    = 0xFF34343Eu; // cellule occupée
    static constexpr D3DCOLOR kColEmptyCell = 0xFF1A1A20u; // cellule vide
    static constexpr D3DCOLOR kColBtnBg     = 0xFF3A3A46u; // bouton actif
    static constexpr D3DCOLOR kColBtnBgOff  = 0xFF262629u; // bouton désactivé

    std::string statusText_; // dernier résultat d'action (échange/retrait), affiché en pied de fenêtre

    net::NetClient* net_ = nullptr; // session réseau optionnelle (cf. Bind())
};

} // namespace ts2::ui

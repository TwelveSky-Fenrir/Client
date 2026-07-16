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
//       -> WarehouseState::SwapCells (échange local, « tri »). AUCUN PAQUET.
//   - clic sur le bouton « Retirer -> Sac » (actif seulement si une cellule est
//     sélectionnée) -> WarehouseState::CommitCellToInventory. AUCUN PAQUET.
//   - clic sur « Valider » -> Net_SendPacket_Op32(nc, 1), INCONDITIONNEL.
//   - fermeture -> Net_SendPacket_Op32(nc, 1) (chemin UI_StorageWin_CommitGrid).
//
// ===========================================================================
// RÉSEAU — RÉÉCRIT le 2026-07-16 (vague W6) sur preuve IDA. L'état précédent
// était FAUX sur trois points, tous corrigés ici :
//
//  1. PAQUET INVENTÉ. L'ancien code émettait Net_SendPacket_Op31 avec kind=5
//     (« tri ») / kind=4 (« retrait »). Le balayage de UI_StorageWin_OnLUp
//     0x5d5400 ne trouve QUE DEUX sites Op31 dans tout le binaire, et aucun
//     n'est l'entrepôt : EA 0x5d576c = case 1 (mon étal) sélecteur 1, et
//     EA 0x5d5dd6 = case 5 (boutique-joueur) sélecteur 2. Les sélecteurs 4 et 5
//     d'Op31 N'EXISTENT NULLE PART. L'entrepôt (mode 2) n'émet JAMAIS Op31.
//
//  2. ÉMISSION PAR ACTION. Le binaire n'émet RIEN pour la manipulation de la
//     grille : UI_StorageWin_OnLDown 0x5d4240 et UI_StorageWin_OnKey 0x5d6330 ne
//     contiennent aucun `call Net_Send*`. Glisser-déposer, échange de cellule et
//     saisie de quantité sont du STAGING 100 % LOCAL. Seuls « Valider » et
//     « Fermer » émettent, et ils émettent Net_SendPacket_Op32(&g_AutoPlayMgr, 1) :
//       - Valider (verrou +24, sprite unk_901064 @ (x+167, y+411)) : EA 0x5d5947,
//         INCONDITIONNEL — aucune garde morph/verrou, aucun verrou posé.
//       - Fermer (verrou +12, sprite unk_8F3798 @ (x+8, y+6)) : EA 0x5d57ce ->
//         UI_StorageWin_CommitGrid(this) 0x5d2f70, dont le case 2 (= ENTREPÔT)
//         déverse la grille 5x5 puis émet Op32(1) à l'EA 0x5d373f.
//     Pagination (verrous +16/+20) : purement locale (page 0..4, EA 0x5d585b /
//     0x5d58dc) — les « onglets » d'entrepôt n'existent pas ; les onglets (+1328)
//     et le déplacement d'or (Net_SendOp110, EA 0x5d5ea3) appartiennent au
//     mode 5 = BOUTIQUE-JOUEUR, pas à l'entrepôt.
//
//  3. CODE MORT. L'ancien SendGridCommit() commençait par `if (!net_) return;`
//     alors que Bind() n'était appelé nulle part -> net_ TOUJOURS nul -> aucune
//     émission possible. Le binaire adresse g_NetClient 0x8156A0 en GLOBAL (les
//     234 builders le lisent directement, jamais en paramètre). On restaure ce
//     pattern via net::GlobalNetClient() (Net/NetClient.h:67-68), renseigné par
//     ConnectGameServer — même idiome que Game/MapWarp.cpp:86. Bind()/net_ sont
//     donc SUPPRIMÉS (aucun appelant : vérifié sur tout l'arbre).
//
// Analogue de a1[2] : le binaire garde l'entrée de UI_StorageWin_OnLUp par
// `if (!*(this+8)) return 0;` (dword_1822998, EA 0x5d540d) et le corps de
// UI_StorageWin_CommitGrid par `if (a1[2])` (EA 0x5d2f7e) — c'est le drapeau
// « fenêtre active ». On prend bOpen_ (Dialog) comme analogue VIVANT de ce
// drapeau : mêmes rôle et emplacement dans le flux.
// ===========================================================================
//
// Règle du projet : ce fichier n'édite AUCUN header existant ; il inclut
// UI/UIManager.h, Game/WarehouseSystem.h et Game/ClientRuntime.h en lecture seule.
#pragma once
#include "UI/UIManager.h"
#include "Game/WarehouseSystem.h"
#include "Game/ClientRuntime.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::ui {

class WarehouseWindow : public Dialog {
public:
    WarehouseWindow();

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
    // Bouton « Valider » = verrou +24 du binaire (sprite unk_901064 @ (x+167, y+411),
    // UI_StorageWin_OnLUp case 2, EA 0x5d592d) : SEULE émission explicite de la
    // fenêtre entrepôt -> Net_SendPacket_Op32(nc, 1) (EA 0x5d5947).
    Rect ValidateButtonRect() const;
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
    void HandleValidateClick();

    // Net_SendPacket_Op32(&g_AutoPlayMgr, 1) — le SEUL paquet de la fenêtre
    // entrepôt (opcode 0x20, 1 champ char émis sur 4 octets LE, total 13).
    // Émis par le bouton « Valider » (EA 0x5d5947) et par la fermeture via
    // UI_StorageWin_CommitGrid case 2 (EA 0x5d373f) — INCONDITIONNEL dans les
    // deux cas. Cible : net::GlobalNetClient() (g_NetClient 0x8156A0 global),
    // cf. bandeau de tête de fichier.
    void SendStorageCommit();

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
    // Deux boutons côte à côte dans le pied de fenêtre (« Retirer -> Sac » et
    // « Valider ») : 2*122 + 8 de gouttière = 252 <= kPanelW - 2*kGridPad = 256.
    static constexpr int kBtnW      = 122;
    static constexpr int kBtnH      = 24;
    static constexpr int kBtnGap    = 8;

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
};

} // namespace ts2::ui

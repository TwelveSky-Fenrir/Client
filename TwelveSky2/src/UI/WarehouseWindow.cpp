// UI/WarehouseWindow.cpp — implémentation de la fenêtre entrepôt.
// Voir UI/WarehouseWindow.h pour le contrat d'interaction et le câblage réseau.
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant
// <windows.h>, qu'UI/WarehouseWindow.h tire transitivement via <d3d9.h>) —
// même convention que UI/ChatWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 puis windows (ordre sur)
#include "Net/NetClient.h"
#include "UI/WarehouseWindow.h"
#include "Asset/ImgFile.h"
#include "Game/GameDatabase.h"

#include <cstdio>

namespace ts2::ui {

namespace {
// Résolveur d'icône d'objet — IDENTIQUE à ResolveItemIconPath de UI/InventoryWindow.cpp
// (pattern de référence de la mission, dupliqué ici faute de header commun sans toucher à
// l'architecture existante). CONFIRMÉ par désassemblage (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md) :
// l'index de fichier N'EST PAS itemId (ancienne hypothèse, FAUSSE) mais le champ SÉPARÉ
// ITEM_INFO+192 ("IconID", game::ItemInfo::iconId, 1-based), lu via game::GetItemInfo().
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}
} // namespace

WarehouseWindow::WarehouseWindow() {
    // Centrage par DÉFAUT (résolution de référence 1024x768), seulement valide avant le
    // tout premier Render(). CORRECTIF (re-vérification mission « fenêtres mal ajustées »,
    // 2026-07-14) : x_/y_ étaient auparavant calculés UNE SEULE FOIS ici, à partir des
    // constantes de conception kRefWidth/kRefHeight, et JAMAIS rafraîchis ensuite — contrairement
    // à EnchantWindow::ComputeLayout (recalculé depuis ctx.screenW/screenH à CHAQUE Render())
    // et MsgBoxDialog::Layout (idem), et contrairement au contrat documenté par
    // Dialog::x_/y_ lui-même (« position écran, recentrée chaque frame », cf.
    // UI/UIManager.h). Résultat du bug : à toute résolution d'affichage RÉELLE différente de
    // 1024x768, le panneau restait figé à la position calculée pour la résolution de
    // conception au lieu d'être recentré sur l'écran réel — exactement le symptôme
    // « coordonnées pétées à une résolution » rapporté. Le recentrage réel se fait
    // maintenant dans RecomputeCenter(), appelé depuis Render(ctx,...) avec ctx.screenW/
    // ctx.screenH (valeurs réelles courantes, cf. UI/UIManager.h::UiContext).
    RecomputeCenter(ts2::kRefWidth, ts2::kRefHeight);
}

// cf. commentaire du constructeur ci-dessus : dimensions du panneau (kPanelW/kPanelH)
// FIXES (relevées sur la grille 5x5 réelle, cf. Game/WarehouseSystem.h), seule
// l'ORIGINE (x_,y_) est recentrée sur la résolution d'affichage réelle courante.
void WarehouseWindow::RecomputeCenter(int screenW, int screenH) {
    x_ = (screenW  - kPanelW) / 2;
    y_ = (screenH - kPanelH) / 2;
}

// ============================================================================
// Cycle de vie
// ============================================================================
void WarehouseWindow::Open() {
    Dialog::Open();
    statusText_.clear();
}

// Fermeture — UI_StorageWin_OnLUp case 2, verrou +12 (EA 0x5d579e/0x5d57ce) : le
// bouton « fermer » (sprite unk_8F3798 @ (x+8, y+6)) appelle UI_StorageWin_CommitGrid(this)
// (EA 0x5d57e4) et RIEN d'autre — contrairement aux cases 1/3/5/7, il n'y a pas de
// cGameHud_Hide ici.
// UI_StorageWin_CommitGrid 0x5d2f70 : `if (a1[2])` (EA 0x5d2f7e) puis `switch (a1[10])`
// (= mode) ; le case 2 = ENTREPÔT (EA 0x5d338c) déverse la grille 5x5 dans les tableaux
// d'inventaire PUIS émet Net_SendPacket_Op32(&g_AutoPlayMgr, 1) — INCONDITIONNEL
// (EA 0x5d373f). bOpen_ tient lieu d'analogue de a1[2] (cf. bandeau de tête du .h).
void WarehouseWindow::Close() {
    if (bOpen_) SendStorageCommit(); // Op32(1) — EA 0x5d373f (via CommitGrid case 2)

    // TODO [ancre 0x5d338c..0x5d3727] : le case 2 de UI_StorageWin_CommitGrid déverse
    // AUSSI la grille 5x5 entière vers g_InvMain/g_InvGrid_*/g_InvAux avant d'émettre.
    // Non reproduit ici À DESSEIN : le modèle C++ committe DÉJÀ par action
    // (WarehouseState::CommitCellToInventory sur « Retirer »), là où le binaire utilise
    // la grille comme tampon de staging déversé en une fois. Appeler ici
    // CommitAllToInventory() en plus déplacerait vers le sac tout objet RESTÉ en
    // entrepôt (le déversement ne filtre que sur `itemId >= 1`, EA 0x5d33de, pas sur
    // une quelconque destination) — effet local non prouvé, donc écarté. Divergence
    // structurelle préexistante de Game/WarehouseSystem.h (non possédé), cf. rapport.

    // Ne laisse pas une sélection « en l'air » entre deux ouvertures.
    game::g_Warehouse.CancelPendingMove();
    statusText_.clear();
    Dialog::Close();
}

// ============================================================================
// Géométrie
// ============================================================================
WarehouseWindow::Rect WarehouseWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

WarehouseWindow::Rect WarehouseWindow::CloseButtonRect() const {
    return { x_ + kPanelW - kGridPad - kCloseSize, y_ + (kHeaderH - kCloseSize) / 2,
             kCloseSize, kCloseSize };
}

WarehouseWindow::Rect WarehouseWindow::WithdrawButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kGridPad, footerTop + 28, kBtnW, kBtnH };
}

// Bouton « Valider » = verrou +24 du binaire (UI_StorageWin_OnLUp case 2, sprite
// unk_901064 testé en (x+167, y+411), EA 0x5d592d).
WarehouseWindow::Rect WarehouseWindow::ValidateButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kPanelW - kGridPad - kBtnW, footerTop + 28, kBtnW, kBtnH };
}

WarehouseWindow::Rect WarehouseWindow::CellRect(int row, int col) const {
    return { x_ + kGridPad + col * (kCellSize + kCellGap),
             y_ + kHeaderH + kGridPad + row * (kCellSize + kCellGap),
             kCellSize, kCellSize };
}

bool WarehouseWindow::CellAt(int mx, int my, int& outRow, int& outCol) const {
    for (int row = 0; row < game::WarehouseGrid::kRows; ++row) {
        for (int col = 0; col < game::WarehouseGrid::kCols; ++col) {
            const Rect r = CellRect(row, col);
            if (PointInRect(mx, my, r.x, r.y, r.w, r.h)) {
                outRow = row;
                outCol = col;
                return true;
            }
        }
    }
    return false;
}

bool WarehouseWindow::PointInPanel(int mx, int my) const {
    return PointInRect(mx, my, x_, y_, kPanelW, kPanelH);
}

// ============================================================================
// Événements souris / clavier
// ============================================================================
bool WarehouseWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    // Consomme simplement le clic-enfoncé s'il tombe dans le panneau, pour
    // empêcher qu'il retombe sur le monde 3D (règle « premier consommateur
    // gagne »). Toute la logique d'action est au relâchement (OnClick), comme
    // documenté dans le contrat Dialog (« clic relâché = validé »).
    return PointInPanel(x, y);
}

bool WarehouseWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    if (!PointInPanel(x, y)) return false;

    const Rect closeBtn = CloseButtonRect();
    if (PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        Close();
        return true;
    }

    int row = -1, col = -1;
    if (CellAt(x, y, row, col)) {
        HandleCellClick(row, col);
        return true;
    }

    if (game::g_Warehouse.pendingMove.active) {
        const Rect wbtn = WithdrawButtonRect();
        if (PointInRect(x, y, wbtn.x, wbtn.y, wbtn.w, wbtn.h)) {
            HandleWithdrawClick();
            return true;
        }
    }

    // « Valider » (verrou +24, EA 0x5d58f8) : toujours actif — le binaire ne
    // conditionne ce bouton à aucun état (ni sélection, ni morph, ni verrou).
    const Rect vbtn = ValidateButtonRect();
    if (PointInRect(x, y, vbtn.x, vbtn.y, vbtn.w, vbtn.h)) {
        HandleValidateClick();
        return true;
    }

    // Clic dans le panneau mais hors zone active (fond, en-tête...) : consommé
    // quand même, la fenêtre est modale de fait pendant qu'elle est ouverte.
    return true;
}

bool WarehouseWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    // Fidélité UI_StorageWin_OnKey 0x5d6330 : fenêtre entrepôt ACTIVE (*(this+2)) => le
    // handler CONSOMME la touche (return 1) sans jamais fermer ni émettre — 0x5d6330 ne
    // contient AUCUN call Net_Send ni UI_StorageWin_CommitGrid. Le commit/close ne part
    // QUE de la souris : bouton X (verrou +12 -> CommitGrid case 2 -> Op32(1), EA 0x5d373f)
    // et bouton Valider (verrou +24 -> Op32(1), EA 0x5d5947). L'ancien code faisait
    // ESC -> Close() -> SendStorageCommit() -> Op32(1), donc émettait un paquet que le
    // binaire N'ENVOIE PAS sur clavier : les 19 xrefs de UI_StorageWin_CommitGrid 0x5d2f70
    // sont TOUTES souris (X, cGameHud_OnMouseUp) ou handlers réseau, AUCUN appelant clavier.
    // On consomme donc ESC SANS fermer (return true, comme 0x5d6330) -> plus d'émission
    // parasite. La fermeture (et son Op32(1) fidèle) reste possible via le bouton X.
    if (vk == VK_ESCAPE) return true;
    return false;
}

// ============================================================================
// Actions
// ============================================================================
void WarehouseWindow::HandleCellClick(int row, int col) {
    game::WarehouseState& wh = game::g_Warehouse;
    game::WarehousePendingMove& pm = wh.pendingMove;

    if (!pm.active) {
        // Rien à faire sur une cellule vide sans sélection en cours.
        if (wh.grid.cells[row][col].Empty()) return;
        wh.SelectPendingMove(row, col);
        statusText_.clear();
        return;
    }

    if (pm.srcRow == row && pm.srcCol == col) {
        // Reclic sur la cellule déjà sélectionnée : désélection.
        wh.CancelPendingMove();
        statusText_.clear();
        return;
    }

    // Reclic sur une AUTRE cellule -> échange PUREMENT LOCAL (« tri »).
    // AUCUNE ÉMISSION — et c'est FIDÈLE : le binaire n'émet rien pour la
    // manipulation de la grille (UI_StorageWin_OnLDown 0x5d4240 et
    // UI_StorageWin_OnKey 0x5d6330 ne contiennent aucun `call Net_Send*` ;
    // balayage exhaustif de la plage 0x5d2770..0x5d8900 : seuls Open, CommitGrid
    // et OnLUp émettent). La grille est un TAMPON DE STAGING côté client ; elle
    // ne part au serveur qu'au « Valider »/« Fermer », sous forme d'Op32(1).
    // L'ancien SendGridCommit(kind=5) émettait un Op31 sélecteur 5 qui N'EXISTE
    // NULLE PART dans le binaire (cf. bandeau de tête du .h).
    if (wh.SwapCells(pm.srcRow, pm.srcCol, row, col)) {
        statusText_ = "Objets echanges.";
    } else {
        statusText_ = "Echange impossible.";
    }
    pm.Clear();
}

// Bouton « Valider » : UI_StorageWin_OnLUp case 2, verrou +24 (EA 0x5d58f8/0x5d592d).
// Émet Net_SendPacket_Op32(&g_AutoPlayMgr, 1) à l'EA 0x5d5947 — INCONDITIONNEL :
// aucune garde morph/verrou (contrairement au case 1/5 qui gardent leur Op31 par
// `g_MorphInProgress == 1 || g_GmCmdCooldownLatch`), et AUCUN verrou posé ensuite
// (ni g_GmCmdCooldownLatch, ni flt_1675B0C) — ne rien ajouter ici.
void WarehouseWindow::HandleValidateClick() {
    SendStorageCommit();
    statusText_ = "Entrepot valide.";
}

void WarehouseWindow::HandleWithdrawClick() {
    game::WarehouseState& wh = game::g_Warehouse;
    game::WarehousePendingMove& pm = wh.pendingMove;
    if (!pm.active) return;

    const int row = pm.srcRow, col = pm.srcCol;

    // Retrait -> commit LOCAL via WarehouseState::CommitCellToInventory (écrit dans
    // game::g_Client.inv, à l'image du déversement de UI_StorageWin_CommitGrid
    // EA 0x5d3438..0x5d3727). AUCUNE ÉMISSION ici — fidèle : dans le binaire, le
    // retrait se fait par glisser-déposer, qui ne produit aucun paquet (le staging
    // ne part qu'au « Valider »/« Fermer »). L'ancien SendGridCommit(kind=4)
    // émettait un Op31 sélecteur 4 INEXISTANT dans le binaire.
    if (wh.CommitCellToInventory(row, col)) {
        statusText_ = "Objet retire vers le sac.";
    } else {
        statusText_ = "Retrait impossible (cellule vide).";
    }
    pm.Clear();
}

// Net_SendPacket_Op32 (Net/SendPackets.cpp:1370, byte-exact vs Net_SendPacket_Op32
// 0x4b64e0) : opcode 0x20, un champ char émis sur 4 octets LE, total 13 octets.
// Le sélecteur est le littéral 1 dans les DEUX sites entrepôt (EA 0x5d5947 et
// EA 0x5d373f) ; le sélecteur 2 existe aussi (EA 0x5d6127) mais appartient au
// case 6 = boutique-joueur, PAS à l'entrepôt.
//
// Cible = net::GlobalNetClient() : le binaire adresse g_NetClient 0x8156A0 en
// GLOBAL (Net_SendPacket_Op32 le lit directement, il ne reçoit aucun socket).
// Ce pointeur est renseigné par ConnectGameServer (Net/Login.cpp:311) — il est
// donc réellement non-nul en session ; ce n'est PAS le `if (!net_)` mort d'avant
// (net_ n'était jamais lié). Même idiome que Game/MapWarp.cpp:86. Hors session
// (self-test Tools/UiWindowSelfTest.cpp), no-op silencieux.
void WarehouseWindow::SendStorageCommit() {
    if (net::NetClient* nc = net::GlobalNetClient())
        net::Net_SendPacket_Op32(*nc, 1);
}

std::string WarehouseWindow::CellLabel(const game::WarehouseItemCell& cell) {
    const game::ItemInfo* info = game::GetItemInfo(cell.itemId);
    if (!info || info->name[0] == '\0') return {};
    std::string s(info->name, strnlen(info->name, sizeof(info->name)));
    if (cell.count > 1) s += " x" + std::to_string(cell.count);
    return s;
}

// ============================================================================
// Icônes (même pattern paresseux+cache que InventoryWindow::GetIconTex)
// ============================================================================
gfx::GpuTexture* WarehouseWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t itemId) {
    // Cache PARTAGÉ par chemin de fichier (cf. SetIconCache/ActiveIconCache) : une icône
    // déjà chargée par InventoryWindow/EnchantWindow/VendorShopWindow est réutilisée sans
    // re-décoder/re-uploader en VRAM (même fichier .IMG, même ITEM_INFO::iconId).
    const std::string path = ResolveItemIconPath(itemId);
    return ActiveIconCache().GetOrLoad(dev, path);
}

// ============================================================================
// Rendu
// ============================================================================
void WarehouseWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Recentrage sur la résolution d'affichage RÉELLE courante — cf. commentaire du
    // constructeur : DOIT s'exécuter avant tout usage de x_/y_ ci-dessous (phase Panels
    // ET phase Text), sinon la phase Text redessinerait sur les rects de l'ANCIENNE
    // frame en cas de redimensionnement entre-temps. Appelé même fenêtre fermée
    // serait inutile (bOpen_ garde tôt) mais inoffensif ; on retourne avant si fermé.
    RecomputeCenter(ctx.screenW, ctx.screenH);
    if (!bOpen_) return;

    const game::WarehouseState& wh = game::g_Warehouse;
    const Rect panel      = PanelRect();
    const Rect closeBtn   = CloseButtonRect();
    const Rect withdrawBt = WithdrawButtonRect();
    const Rect validateBt = ValidateButtonRect();
    const bool hasSel     = wh.pendingMove.active;

    if (ctx.phase == UiPhase::Panels) {
        // Fond + cadre du panneau.
        ctx.FillRect(panel.x, panel.y, panel.w, panel.h, kColPanelBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        // Bandeau titre.
        ctx.FillRect(panel.x, panel.y, panel.w, kHeaderH, kColHeaderBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kHeaderH, kColFrame, 1);

        // Bouton fermer (croix), survol en rouge.
        const bool closeHover = PointInRect(cursorX, cursorY, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h);
        ctx.FillRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, closeHover ? kColError : kColBtnBg);
        ctx.DrawFrame(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, kColFrame, 1);

        // Grille 5x5. Icône réelle (.IMG, résolue par itemId) si dispo, sinon repli sur la
        // cellule pleine colorée d'origine. Survol d'une cible potentielle -> cadre/fond bleu
        // (kColSelect). La case SOURCE d'un glisser en cours n'a PAS de traitement particulier
        // ici : elle est rendue EXACTEMENT comme une case vide (cf. `selected` ci-dessous) —
        // CONFIRMÉ par désassemblage (Item_BeginDragTransaction 0x5AFDF0 +
        // Inv_RemoveItemQuantity 0x5B0340 case 18/19/27/28 : l'objet saisi est retiré de la
        // grille source dès le clic ; UI_StorageWin_Draw 0x5D6610 ne dessine l'icône QUE si
        // `champ >= 1`, donc RIEN n'est dessiné pour cette case tant que le glisser est actif —
        // aucune teinte grisée, cf. bandeau de tête de UI/WarehouseWindow.h). L'objet saisi est
        // dessiné séparément, collé au curseur, plus bas.
        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;
        for (int row = 0; row < game::WarehouseGrid::kRows; ++row) {
            for (int col = 0; col < game::WarehouseGrid::kCols; ++col) {
                const Rect r = CellRect(row, col);
                const game::WarehouseItemCell& cell = wh.grid.cells[row][col];
                const bool selected = hasSel && wh.pendingMove.srcRow == row && wh.pendingMove.srcCol == col;
                // `selected` compte comme vide pour le RENDU (l'objet est toujours en mémoire
                // dans pendingMove.snapshot pour l'annulation/l'échange, mais l'écran doit le
                // montrer retiré, comme dans le binaire).
                const bool empty    = cell.Empty() || selected;
                const bool hovered  = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
                const bool highlight = hovered && (!empty || hasSel);

                gfx::GpuTexture* icon = empty ? nullptr : GetIconTex(dev, cell.itemId);
                if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                    ctx.FillRect(r.x, r.y, r.w, r.h, kColCellBg); // fond neutre sous l'icône
                    const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                    const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                    ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                                   gfx::kSpriteWhite, /*compensatePos=*/true);
                } else {
                    D3DCOLOR bg = empty ? kColEmptyCell : kColCellBg;
                    if (highlight) bg = kColSelect;
                    ctx.FillRect(r.x, r.y, r.w, r.h, bg);
                }

                const D3DCOLOR frameCol = (icon && highlight) ? kColSelect : kColFrame;
                const int      frameT   = (icon && highlight) ? 2 : 1;
                ctx.DrawFrame(r.x, r.y, r.w, r.h, frameCol, frameT);
            }
        }

        // Objet en cours de glissement, collé au curseur — CONFIRMÉ par désassemblage
        // (maybe_UI_QuickSlotBar_Render 0x5BE340, seule fonction qui référence g_DragCtx
        // 0x1822380 en LECTURE pour du rendu — appelée par UI_RenderAllDialogs 0x5AE2D0
        // APRÈS UI_StorageWin_Draw, donc par-dessus la grille : cas 27/28 = type entrepôt
        // dans le switch sur `g_DragCtx.kind`. L'icône y est dessinée CENTRÉE sur le curseur
        // (`a4 - largeur/2, a5 - hauteur/2`, PAS d'offset de préhension), absente de
        // l'implémentation précédente qui ne dessinait QUE la case source grisée — c'était
        // l'inverse du binaire (rien à la source, icône au curseur). Ajouté ici.
        if (hasSel && dev && ctx.sprites) {
            gfx::GpuTexture* dragIcon = GetIconTex(dev, wh.pendingMove.snapshot.itemId);
            if (dragIcon && dragIcon->Handle() && dragIcon->Width() > 0 && dragIcon->Height() > 0) {
                const int dx = cursorX - kCellSize / 2;
                const int dy = cursorY - kCellSize / 2;
                const float sx = static_cast<float>(kCellSize) / static_cast<float>(dragIcon->Width());
                const float sy = static_cast<float>(kCellSize) / static_cast<float>(dragIcon->Height());
                ctx.sprites->DrawSpriteScaled(dragIcon->Handle(), nullptr, dx, dy, sx, sy,
                                               gfx::kSpriteWhite, /*compensatePos=*/true);
            }
        }

        // Bouton « Retirer -> Sac » (actif seulement si une cellule est sélectionnée).
        const bool wbtnHover = hasSel && PointInRect(cursorX, cursorY, withdrawBt.x, withdrawBt.y, withdrawBt.w, withdrawBt.h);
        ctx.FillRect(withdrawBt.x, withdrawBt.y, withdrawBt.w, withdrawBt.h,
                     !hasSel ? kColBtnBgOff : (wbtnHover ? kColSelect : kColBtnBg));
        ctx.DrawFrame(withdrawBt.x, withdrawBt.y, withdrawBt.w, withdrawBt.h, kColFrame, 1);

        // Bouton « Valider » (verrou +24, EA 0x5d592d) : TOUJOURS actif — le binaire
        // ne le conditionne à aucun état.
        const bool vbtnHover = PointInRect(cursorX, cursorY, validateBt.x, validateBt.y, validateBt.w, validateBt.h);
        ctx.FillRect(validateBt.x, validateBt.y, validateBt.w, validateBt.h,
                     vbtnHover ? kColSelect : kColBtnBg);
        ctx.DrawFrame(validateBt.x, validateBt.y, validateBt.w, validateBt.h, kColFrame, 1);
        return;
    }

    // --- Phase texte ---
    ctx.Text("Entrepot", panel.x + kGridPad, panel.y + (kHeaderH - 12) / 2, kColTitle);

    const int closeLblW = ctx.MeasureText("X");
    ctx.Text("X", closeBtn.x + (closeBtn.w - closeLblW) / 2, closeBtn.y + 2, kColText);

    for (int row = 0; row < game::WarehouseGrid::kRows; ++row) {
        for (int col = 0; col < game::WarehouseGrid::kCols; ++col) {
            const game::WarehouseItemCell& cell = wh.grid.cells[row][col];
            if (cell.Empty()) continue;
            const Rect r = CellRect(row, col);
            const std::string label = CellLabel(cell);
            ctx.Text(label.c_str(), r.x + 3, r.y + 3, kColText);
            if (cell.durability > 0) {
                const std::string dur = "d:" + std::to_string(cell.durability);
                ctx.Text(dur.c_str(), r.x + 3, r.y + kCellSize - 15, kColTextDim);
            }
        }
    }

    // Quantité de l'objet en cours de glissement, sous l'icône collée au curseur
    // (cf. phase Panels ci-dessus) — même condition (hasSel), même source de
    // données (pendingMove.snapshot).
    if (hasSel && wh.pendingMove.snapshot.count > 1) {
        const std::string qty = "x" + std::to_string(wh.pendingMove.snapshot.count);
        ctx.Text(qty.c_str(), cursorX - kCellSize / 2 + 2, cursorY + kCellSize / 2 - 14, kColText);
    }

    // Pied de fenêtre : poids / monnaie (g_Client.inv, cf. Game/ClientRuntime.h).
    const int footerTop = panel.y + panel.h - kFooterH;
    char line[96];
    std::snprintf(line, sizeof(line), "Poids: %lld    Or: %lld",
                  static_cast<long long>(game::g_Client.inv.weight),
                  static_cast<long long>(game::g_Client.inv.currency));
    ctx.Text(line, panel.x + kGridPad, footerTop + 4, kColText);

    // Libellé du bouton retirer.
    const char* btnLabel = hasSel ? "Retirer -> Sac" : "(selectionner)";
    const int btnLabelW = ctx.MeasureText(btnLabel);
    ctx.Text(btnLabel, withdrawBt.x + (withdrawBt.w - btnLabelW) / 2,
              withdrawBt.y + (withdrawBt.h - 12) / 2, hasSel ? kColText : kColTextDim);

    // Libellé du bouton valider (seul émetteur explicite de la fenêtre, EA 0x5d5947).
    const int vLabelW = ctx.MeasureText("Valider");
    ctx.Text("Valider", validateBt.x + (validateBt.w - vLabelW) / 2,
              validateBt.y + (validateBt.h - 12) / 2, kColText);

    // Dernier statut d'action (échange/retrait), sous le bouton.
    if (!statusText_.empty())
        ctx.Text(statusText_.c_str(), panel.x + kGridPad, footerTop + kFooterH - 14, kColSuccess);
}

} // namespace ts2::ui

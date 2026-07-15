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

void WarehouseWindow::Close() {
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
    return { x_ + (kPanelW - kBtnW) / 2, footerTop + 28, kBtnW, kBtnH };
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

    // Clic dans le panneau mais hors zone active (fond, en-tête...) : consommé
    // quand même, la fenêtre est modale de fait pendant qu'elle est ouverte.
    return true;
}

bool WarehouseWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
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

    // Reclic sur une AUTRE cellule de la grille -> échange local (« tri »),
    // à l'image de UI_StorageWin_Open mode 5 / UI_StorageWin_CommitGrid, PUIS
    // envoi réseau RÉEL du commit de grille via SendGridCommit(kind=5) — voir
    // le commentaire de tête de fichier (Net_SendPacket_Op31, seul builder dont
    // la charge utile correspond EXACTEMENT à sizeof(WarehouseGrid)). Le
    // serveur répondrait normalement par Pkt_WarehouseUpdate (déjà géré côté
    // handlers) ; l'échange affiché reste local (immédiat) en attendant ce
    // round-trip, comme le fait le client d'origine (prédiction optimiste).
    if (wh.SwapCells(pm.srcRow, pm.srcCol, row, col)) {
        SendGridCommit(/*kind=tri*/ 5);
        statusText_ = net_ ? "Objets echanges (envoye au serveur)."
                            : "Objets echanges (local, reseau non lie).";
    } else {
        statusText_ = "Echange impossible.";
    }
    pm.Clear();
}

void WarehouseWindow::HandleWithdrawClick() {
    game::WarehouseState& wh = game::g_Warehouse;
    game::WarehousePendingMove& pm = wh.pendingMove;
    if (!pm.active) return;

    const int row = pm.srcRow, col = pm.srcCol;

    // Retrait -> commit LOCAL via WarehouseState::CommitCellToInventory (écrit
    // dans game::g_Client.inv exactement comme le ferait le binaire, cf.
    // UI_StorageWin_CommitGrid dans WarehouseSystem.cpp) PUIS envoi réseau réel
    // du commit de grille via SendGridCommit(kind=4). Le serveur validerait
    // normalement la place disponible dans le sac avant de renvoyer
    // Pkt_WarehouseUpdate (0x24, déjà routé) — le retrait affiché reste local
    // en attendant ce round-trip.
    if (wh.CommitCellToInventory(row, col)) {
        SendGridCommit(/*kind=retrait*/ 4);
        statusText_ = net_ ? "Objet retire vers le sac (envoye au serveur)."
                            : "Objet retire vers le sac (local, reseau non lie).";
    } else {
        statusText_ = "Retrait impossible (cellule vide).";
    }
    pm.Clear();
}

// Net_SendPacket_Op31 (Net/SendPackets.h) : opcode sortant 0x1f, payload
// `int8_t kind + 1232 o`. `game::g_Warehouse.grid` EST le blob byte-exact
// (static_assert sizeof(WarehouseGrid)==1232 dans Game/WarehouseSystem.h) :
// aucune sérialisation à faire, on passe directement son adresse. No-op
// silencieux si net_ n'est pas lié (cf. Bind() / commentaire de tête de fichier).
void WarehouseWindow::SendGridCommit(int8_t kind) {
    if (!net_) return;
    net::Net_SendPacket_Op31(*net_, kind, &game::g_Warehouse.grid);
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
    const char* btnLabel = hasSel ? "Retirer -> Sac" : "(selectionner un objet)";
    const int btnLabelW = ctx.MeasureText(btnLabel);
    ctx.Text(btnLabel, withdrawBt.x + (withdrawBt.w - btnLabelW) / 2,
              withdrawBt.y + (withdrawBt.h - 12) / 2, hasSel ? kColText : kColTextDim);

    // Dernier statut d'action (échange/retrait), sous le bouton.
    if (!statusText_.empty())
        ctx.Text(statusText_.c_str(), panel.x + kGridPad, footerTop + kFooterH - 14, kColSuccess);
}

} // namespace ts2::ui

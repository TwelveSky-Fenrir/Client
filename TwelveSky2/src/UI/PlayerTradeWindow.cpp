// UI/PlayerTradeWindow.cpp — implémentation de la fenêtre ÉCHANGE ENTRE JOUEURS.
// Voir UI/PlayerTradeWindow.h pour le détail des écarts documentés (grille non
// prouvée, câblage Open()/Close() non fait côté handlers réseau).
#include "UI/PlayerTradeWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameDatabase.h"    // game::GetItemInfo / ItemInfo::iconId (icônes réelles)
#include "Gfx/Renderer.h"         // ctx.renderer->Device()
#include "Gfx/IconTextureCache.h" // cache d'icônes .IMG (file-local, cf. modèle d'échange ci-dessous)

#include <cstdio>
#include <cstring> // std::strcmp (sélection self/partenaire par libellé de grille)

namespace ts2::ui {

// ===========================================================================
// Adresses d'origine — DUPLIQUÉES depuis Net/GameHandlers_VendorTrade.cpp
// (fichier interdit d'édition pour cette mission). Mêmes valeurs, même rôle :
// voir le commentaire de tête de ce fichier-là pour la sémantique détaillée.
// ===========================================================================
namespace {
// Fond de panneau réel (best effort) : gabarit (446,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec le panneau Échange (344x310 ; cf. méthodologie
// détaillée dans UI/PanelSkin.h). Indice distinct de ceux utilisés par
// GuildWindow/CharacterStatsWindow (même cluster de tailles, fichiers
// différents). Repli automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01341.IMG");

constexpr uint32_t kTradePartnerIdLo = 0x168741C; // g_TradePartnerIdLo (partenaire mot 0)
constexpr uint32_t kTradePartnerVal1 = 0x1687420; // dword_1687420      (partenaire mot 1)
// dword_1687424 : 3e mot de données PARTENAIRE écrit par le serveur (0x8156C1+8),
// via Pkt_TradeRequestPrompt 0x48FD20 (@0x48FDA0 dword_1687424[0]=v7[2]). Comparé au
// code d'action par Pkt_TradeActionResult 0x48FEA0 (@0x48FEFE 'if(dword_1687424==v7)').
// PAS un drapeau d'accord local (interprétation W4-F3 réfutée).
constexpr uint32_t kTradePartnerWord2 = 0x1687424; // dword_1687424 (partenaire mot 2)
// CORRECTIF W4-F3 (data_refs sur 0x1675B24), RE-CONFIRMÉ W6 par décompilation fraîche :
// l'ancien kTradeState=0x1675B24 était FAUX. Ce global est g_PendingOrderKind (type
// d'ordre monde / ciblage : AutoPlay_UpdateTargeting 0x45D080, Game_OnWorldLeftClick
// 0x536690, Player_InteractWithPlayer 0x5392E0, Player_CastSkill 0x53BC40) — RIEN à voir
// avec l'échange. Il n'est mis à 0 par Pkt_TradeRequestPrompt 0x48FD20 (@0x48fd82) /
// Pkt_TradeActionResult 0x48FEA0 (@0x48ff66) que comme effet de bord (reset du ciblage à
// l'arrivée d'un prompt d'échange). Supprimé -> ne plus l'afficher comme « état d'échange ».
//
// kTradeExtra=0x1675D84 : également retiré de l'affichage, mais PAS pour la raison
// notée en W4-F3 (« jamais prouvé ») — formulation inexacte, corrigée ici. Son ÉCRITURE
// est bel et bien prouvée : Pkt_TradeRequestPrompt le charge depuis 0x8156D1
// (`dword_1675D84 = v5` @0x48fdac) et Pkt_TradeActionResult le force à 1 (@0x48ff8e).
// C'est sa SÉMANTIQUE qui reste indéterminée -> on ne l'affiche pas plutôt que de lui
// prêter un rôle inventé (règle « ne jamais deviner »).

// ===========================================================================
// Modèle d'échange — POSSÉDÉ par ce .cpp (PlayerTradeWindow.h ne peut pas porter de
// membre cache/état sans divergence de layout). Objets offerts par soi / le partenaire
// + verrous de moitié.
//
// ⚠️ CE MODÈLE EST UNE RÉINVENTION, ET IL RESTERA VIDE POUR TOUJOURS. Ce n'est pas un
// câblage en attente : le binaire n'a AUCUNE table d'échange, donc aucun handler ne
// peuplera jamais ces tableaux (preuves exhaustives en tête de UI/PlayerTradeWindow.h).
// L'ancien « TODO [handler 0x48FD20/0x48FEA0] à relever en dynamique x32dbg » posé en
// W4-F3 laissait croire à une grille d'origine à retrouver : SUPPRIMÉ, il était sans
// objet. Les 3 handlers d'échange ne peuplent aucune grille PARCE QU'AUCUNE GRILLE
// N'EXISTE. Le rendu ci-dessous est donc structurellement une coquille vide, conservée
// uniquement parce que la fenêtre est instanciée par UI/GameWindows.h:190 (header non
// possédé par ce front). Sa suppression relève d'une décision d'architecture.
struct TradeCell { uint32_t itemId = 0, count = 0, color = 0, durability = 0; };
constexpr int kTradeCols  = 5;                     // == PlayerTradeWindow::kGridCols
constexpr int kTradeRows  = 5;                     // == PlayerTradeWindow::kGridRows
constexpr int kTradeCells = kTradeCols * kTradeRows; // 25
struct TradeModel {
    TradeCell self[kTradeCells];
    TradeCell partner[kTradeCells];
    bool selfLocked = false, partnerLocked = false;
};
TradeModel g_tradeModel;

// Cache d'icônes .IMG file-local (PlayerTradeWindow.h n'a pas de membre cache, cf.
// InventoryWindow::sharedIconCache_ ; on ne peut pas l'injecter sans éditer le .h).
// Clé par chemin de fichier, même politique de lazy-load/échec-mis-en-cache que les
// autres fenêtres (Gfx/IconTextureCache.h).
gfx::IconTextureCache g_tradeIconCache;

// Icône réelle d'un objet offert : chemin dérivé d'ITEM_INFO::iconId (+192, 1-based),
// gabarit "002\002_%05u.IMG" — MÊME pattern confirmé que ConsumableBar/Inventory/
// VendorShop (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md). nullptr si device indispo, objet
// hors ITEM_INFO, ou iconId nul.
gfx::GpuTexture* ResolveTradeIcon(const UiContext& ctx, uint32_t itemId) {
    if (itemId == 0 || !ctx.renderer || !ctx.renderer->Device()) return nullptr;
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return nullptr;
    char path[64];
    std::snprintf(path, sizeof(path), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return g_tradeIconCache.GetOrLoad(ctx.renderer->Device(), path);
}
} // namespace

// ===========================================================================
// Cycle de vie
// ===========================================================================
void PlayerTradeWindow::Open() {
    Dialog::Open(); // bOpen_ = true
    closePressed_ = acceptPressed_ = cancelPressed_ = false;
}

void PlayerTradeWindow::Close() {
    Dialog::Close(); // bOpen_ = false
    closePressed_ = acceptPressed_ = cancelPressed_ = false;
}

// ===========================================================================
// Géométrie
// ===========================================================================
void PlayerTradeWindow::Layout(int screenW, int screenH, Rect& box, Rect& closeBtn,
                                Rect& acceptBtn, Rect& cancelBtn,
                                Rect& selfGrid, Rect& partnerGrid) const {
    box.x = screenW / 2 - kPanelW / 2;
    box.y = screenH / 2 - kPanelH / 2;
    box.w = kPanelW;
    box.h = kPanelH;

    // Bouton fermer (X), coin haut-droit du bandeau titre.
    closeBtn = { box.x + box.w - kCloseSize - 6, box.y + (kHeaderH - kCloseSize) / 2,
                 kCloseSize, kCloseSize };

    // Deux grilles côte à côte, sous le bandeau titre + libellé.
    const int gridsY = box.y + kHeaderH + kPanelPad + kGridLabelH;
    selfGrid    = { box.x + kPanelPad,                          gridsY, kGridW, kGridH };
    partnerGrid = { box.x + kPanelPad + kGridW + kGridGap,       gridsY, kGridW, kGridH };

    // Rangée de boutons en pied de fenêtre.
    const int btnY = box.y + box.h - kPanelPad - kBtnH;
    acceptBtn = { box.x + box.w / 2 - kBtnW - 8, btnY, kBtnW, kBtnH };
    cancelBtn = { box.x + box.w / 2 + 8,         btnY, kBtnW, kBtnH };
}

// ===========================================================================
// Rendu
// ===========================================================================
void PlayerTradeWindow::DrawGridPlaceholder(const UiContext& ctx, const Rect& grid,
                                             const char* label) const {
    // Sélectionne la moitié du modèle d'échange (self vs partenaire) via le libellé
    // fourni par les deux seuls sites d'appel de ce fichier ("Vous" / "Partenaire").
    const bool isSelf = (label && std::strcmp(label, "Vous") == 0);
    const TradeCell* cells = isSelf ? g_tradeModel.self : g_tradeModel.partner;
    const bool locked = isSelf ? g_tradeModel.selfLocked : g_tradeModel.partnerLocked;

    // --- Phase panneaux : cadre extérieur + rendu réel des kGridRows x kGridCols
    //     cellules (fond, cadre, icône .IMG de l'objet offert si présent) ---
    if (ctx.phase == UiPhase::Panels) {
        // Un cadre verrouillé (moitié validée) est teinté pour le signaler.
        ctx.DrawFrame(grid.x - 2, grid.y - 2, grid.w + 4, grid.h + 4,
                      locked ? kColHover : kColFrame, locked ? 2 : 1);
        for (int r = 0; r < kGridRows; ++r) {
            for (int c = 0; c < kGridCols; ++c) {
                const int cx = grid.x + c * (kCellSize + kCellGap);
                const int cy = grid.y + r * (kCellSize + kCellGap);
                const TradeCell& cell = cells[r * kGridCols + c];

                ctx.FillRect(cx, cy, kCellSize, kCellSize, kColCellBg);

                if (cell.itemId != 0) {
                    gfx::GpuTexture* icon = ResolveTradeIcon(ctx, cell.itemId);
                    if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 &&
                        ctx.sprites && ctx.sprites->Ready()) {
                        const float sx = static_cast<float>(kCellSize) / static_cast<float>(icon->Width());
                        const float sy = static_cast<float>(kCellSize) / static_cast<float>(icon->Height());
                        ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, cx, cy, sx, sy,
                                                      gfx::kSpriteWhite, /*compensatePos=*/true);
                    } else {
                        // Repli SANS icône = case pleine accentuée (même politique que
                        // VendorShopWindow quand la résolution d'icône échoue).
                        ctx.FillRect(cx, cy, kCellSize, kCellSize, kColBtnBg);
                    }
                }

                ctx.DrawFrame(cx, cy, kCellSize, kCellSize, kColFrame, 1);
            }
        }
        return;
    }

    // --- Phase texte : libellé au-dessus + badge quantité par cellule occupée ---
    const int labelW = ctx.MeasureText(label);
    ctx.Text(label, grid.x + (grid.w - labelW) / 2, grid.y - kGridLabelH, kColTitle);

    for (int r = 0; r < kGridRows; ++r) {
        for (int c = 0; c < kGridCols; ++c) {
            const TradeCell& cell = cells[r * kGridCols + c];
            if (cell.itemId == 0 || cell.count <= 1) continue;
            const int cx = grid.x + c * (kCellSize + kCellGap);
            const int cy = grid.y + r * (kCellSize + kCellGap);
            char qty[12];
            std::snprintf(qty, sizeof(qty), "%u", cell.count);
            const int qw = ctx.MeasureText(qty);
            ctx.Text(qty, cx + kCellSize - qw - 1, cy + kCellSize - 12, kColText);
        }
    }

    if (locked) {
        const char* lk = "verrouillé";
        const int lw = ctx.MeasureText(lk);
        ctx.Text(lk, grid.x + (grid.w - lw) / 2, grid.y + grid.h + 4, kColHover);
    }
}

void PlayerTradeWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Mémorise les dims écran courantes (hit-test routé entre deux frames),
    // comme MsgBoxDialog::Render.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Rect box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid;
    Layout(ctx.screenW, ctx.screenH, box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid);

    // Lecture LIVE des 2 premiers mots d'IDENTITÉ DU PARTENAIRE (jamais mise en cache :
    // ces globals changent entre deux frames via les handlers réseau). Ce n'est PAS un
    // « état d'échange » : aucun état de ce genre n'existe dans le binaire (cf. en-tête
    // de UI/PlayerTradeWindow.h). Écrits par Pkt_TradeRequestPrompt 0x48FD20
    // (@0x48fd8f/0x48fd97) et remis à 0 par Pkt_TradeActionResult 0x48FEA0
    // (@0x48ff70/0x48ff7a).
    const int32_t partnerIdLo = game::g_Client.VarGet(kTradePartnerIdLo);
    const int32_t partnerVal1 = game::g_Client.VarGet(kTradePartnerVal1);
    const bool    hasPartner  = (partnerIdLo != 0) || (partnerVal1 != 0);

    if (ctx.phase == UiPhase::Panels) {
        // Fond + cadre + bandeau titre.
        kPanelBg.Draw(ctx, box.x, box.y, box.w, box.h, kColBg);
        ctx.FillRect(box.x, box.y, box.w, kHeaderH, kColHeaderBg);
        ctx.DrawFrame(box.x, box.y, box.w, box.h, kColFrame, 2);
        ctx.DrawFrame(box.x, box.y, box.w, kHeaderH, kColFrame, 1);

        // Bouton fermer (X).
        const bool closeHover = PointInRect(cursorX, cursorY, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h);
        ctx.FillRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h,
                     closePressed_ ? kColBtnDown : (closeHover ? kColDanger : kColCloseBg));
        ctx.DrawFrame(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, kColFrame, 1);

        // Deux grilles placeholder.
        DrawGridPlaceholder(ctx, selfGrid, "Vous");
        DrawGridPlaceholder(ctx, partnerGrid, "Partenaire");

        // Boutons Accepter / Annuler.
        const bool acceptHover = PointInRect(cursorX, cursorY, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h);
        ctx.FillRect(acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h,
                     acceptPressed_ ? kColBtnDown : (acceptHover ? kColHover : kColBtnBg));
        ctx.DrawFrame(acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h, kColFrame, 1);

        const bool cancelHover = PointInRect(cursorX, cursorY, cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h);
        ctx.FillRect(cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h,
                     cancelPressed_ ? kColBtnDown : (cancelHover ? kColDanger : kColBtnBg));
        ctx.DrawFrame(cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h, kColFrame, 1);
        return;
    }

    // --- Phase texte ---
    // Titre centré dans le bandeau.
    const char* title = "Échange";
    const int titleW = ctx.MeasureText(title);
    ctx.Text(title, box.x + (box.w - titleW) / 2, box.y + (kHeaderH - 12) / 2, kColTitle);
    ctx.Text("X", closeBtn.x + 5, closeBtn.y + 2, kColText);

    // Ligne de diagnostic partenaire/accord (au-dessus de la rangée de boutons).
    char line1[96];
    if (hasPartner) {
        std::snprintf(line1, sizeof(line1), "Partenaire : #%d / #%d", partnerIdLo, partnerVal1);
    } else {
        std::snprintf(line1, sizeof(line1), "En attente d'une invitation d'échange...");
    }
    const int infoY = box.y + box.h - kPanelPad - kBtnH - kInfoH;
    ctx.Text(line1, box.x + kPanelPad, infoY, kColText);
    // Label « Accord local : oui/non » RETIRÉ : 0x1687424 (kTradePartnerWord2) n'est pas
    // un drapeau d'accord local mais le 3e mot de données partenaire fourni par le serveur,
    // comparé au code d'action par Pkt_TradeActionResult 0x48FEA0 (@0x48FEFE). (W4-F3 réfuté.)

    // Libellés des boutons.
    const char* acceptLbl = "Accepter";
    const int acceptLblW = ctx.MeasureText(acceptLbl);
    ctx.Text(acceptLbl, acceptBtn.x + (acceptBtn.w - acceptLblW) / 2, acceptBtn.y + 6, kColText);

    const char* cancelLbl = "Annuler";
    const int cancelLblW = ctx.MeasureText(cancelLbl);
    ctx.Text(cancelLbl, cancelBtn.x + (cancelBtn.w - cancelLblW) / 2, cancelBtn.y + 6, kColText);
}

// ===========================================================================
// Actions boutons
// ===========================================================================
// ===========================================================================
// PAS D'ÉMISSION ICI — ET C'EST LA RÉPONSE FIDÈLE, PAS UN TROU.
// ===========================================================================
// L'ancien « TODO(send) : builder lock/accept non trouvé » est SUPPRIMÉ : il posait
// une question sans objet. Il n'existe AUCUN paquet « verrouiller / accepter /
// annuler un échange » dans TwelveSky2.exe — pas parce qu'on ne l'a pas trouvé, mais
// parce que le binaire n'a pas de table d'échange du tout (preuves exhaustives en tête
// de UI/PlayerTradeWindow.h : UI_InitAllDialogs 0x5ABF50 + UI_StorageWin_Open 0x5D27A0
// + les 3 handlers purement textuels 0x48FD20/0x48FE10/0x48FEA0).
// Le SEUL sortant du domaine « échange » est Net_SendOp43 (op 0x2B, [nom13@9][flag
// i32@22], len 26 — Net/SendPackets.h:75), et ses 2 uniques xrefs sont les deux boutons
// de UI_ClanWin_OnLUp 0x5D92A0 page 2 (@0x5D9F8A flag=2, @0x5DA0F1 flag=1) : c'est le
// MENU CONTEXTUEL JOUEUR qui l'émet, pas cette fenêtre. Ce front ne possède pas ce
// fichier -> reporté à la vague (cf. rapport W6, section « à câbler ailleurs »).
// Net_SendOp49/50/51 (sortants 0x31/0x32/0x33) restent ÉCARTÉS : coïncidence numérique
// seule — Net_SendOp49 est le flux confirmé « réponse d'invitation d'ALLIANCE »
// (UI_MsgBox_OnLButtonUp @0x5c109d). Ne pas les rebrancher ici.
void PlayerTradeWindow::HandleAccept() {
    Close();
}

void PlayerTradeWindow::HandleCancel() {
    Close();
}

// ===========================================================================
// Événements souris / clavier
// ===========================================================================
bool PlayerTradeWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Rect box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid;
    Layout(lastScreenW_, lastScreenH_, box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid);

    if (PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        closePressed_ = true;
    } else if (PointInRect(x, y, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h)) {
        acceptPressed_ = true;
    } else if (PointInRect(x, y, cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h)) {
        cancelPressed_ = true;
    }
    return true; // modal : consomme tout clic tant qu'ouverte (comme MsgBoxDialog)
}

bool PlayerTradeWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    Rect box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid;
    Layout(lastScreenW_, lastScreenH_, box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid);

    if (closePressed_ && PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        HandleCancel(); // fermer via X = annuler l'échange (comportement UX standard)
    } else if (acceptPressed_ && PointInRect(x, y, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h)) {
        HandleAccept();
    } else if (cancelPressed_ && PointInRect(x, y, cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h)) {
        HandleCancel();
    }
    closePressed_ = acceptPressed_ = cancelPressed_ = false;
    return true; // modal
}

bool PlayerTradeWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { HandleCancel(); return true; }
    if (vk == VK_RETURN) { HandleAccept(); return true; }
    return true; // modal : avale toutes les touches tant qu'ouverte
}

} // namespace ts2::ui

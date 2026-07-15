// UI/PlayerTradeWindow.cpp — implémentation de la fenêtre ÉCHANGE ENTRE JOUEURS.
// Voir UI/PlayerTradeWindow.h pour le détail des écarts documentés (grille non
// prouvée, câblage Open()/Close() non fait côté handlers réseau).
#include "UI/PlayerTradeWindow.h"
#include "UI/PanelSkin.h"

#include <cstdio>

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

constexpr uint32_t kTradePartnerIdLo = 0x168741C; // g_TradePartnerIdLo (partner[0])
constexpr uint32_t kTradePartnerVal1 = 0x1687420; // dword_1687420      (partner[1])
constexpr uint32_t kTradePartnerVal2 = 0x1687424; // dword_1687424 (accord local)
constexpr uint32_t kTradeState       = 0x1675B24; // dword_1675B24 (état d'échange, jamais armé par les handlers actuels)
constexpr uint32_t kTradeExtra       = 0x1675D84; // dword_1675D84
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
    // --- Phase panneaux : cadre extérieur + 25 cellules vides ---
    if (ctx.phase == UiPhase::Panels) {
        ctx.DrawFrame(grid.x - 2, grid.y - 2, grid.w + 4, grid.h + 4, kColFrame, 1);
        for (int r = 0; r < kGridRows; ++r) {
            for (int c = 0; c < kGridCols; ++c) {
                const int cx = grid.x + c * (kCellSize + kCellGap);
                const int cy = grid.y + r * (kCellSize + kCellGap);
                ctx.FillRect(cx, cy, kCellSize, kCellSize, kColCellBg);
                ctx.DrawFrame(cx, cy, kCellSize, kCellSize, kColFrame, 1);
            }
        }
        return;
    }

    // --- Phase texte : libellé au-dessus + note "25 cellules" en dessous ---
    const int labelW = ctx.MeasureText(label);
    ctx.Text(label, grid.x + (grid.w - labelW) / 2, grid.y - kGridLabelH, kColTitle);

    char note[48];
    std::snprintf(note, sizeof(note), "%d cellules (placeholder)", kGridRows * kGridCols);
    const int noteW = ctx.MeasureText(note);
    ctx.Text(note, grid.x + (grid.w - noteW) / 2, grid.y + grid.h + 4, kColTextDim);
}

void PlayerTradeWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Mémorise les dims écran courantes (hit-test routé entre deux frames),
    // comme MsgBoxDialog::Render.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Rect box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid;
    Layout(ctx.screenW, ctx.screenH, box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid);

    // Lecture LIVE de l'état d'échange (jamais mise en cache : ces globals
    // peuvent changer entre deux frames via les handlers réseau).
    const int32_t partnerIdLo = game::g_Client.VarGet(kTradePartnerIdLo);
    const int32_t partnerVal1 = game::g_Client.VarGet(kTradePartnerVal1);
    const int32_t localAgree  = game::g_Client.VarGet(kTradePartnerVal2);
    const int32_t rawState    = game::g_Client.VarGet(kTradeState);
    const int32_t extra       = game::g_Client.VarGet(kTradeExtra);
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

    char line2[96];
    std::snprintf(line2, sizeof(line2), "Accord local : %s   (état brut=%d, extra=%d)",
                  (localAgree != 0) ? "oui" : "non", rawState, extra);
    ctx.Text(line2, box.x + kPanelPad, infoY + 16, kColTextDim);

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
void PlayerTradeWindow::HandleAccept() {
    // TODO(send): valider/verrouiller sa moitié de l'échange. AUCUN builder
    // outbound n'a été identifié avec certitude pour cette action dans
    // RE/outbound_results.json ni Net/SendPackets.h — les seuls candidats dont
    // l'opcode sortant coïncide numériquement avec 0x31/0x32/0x33 (Net_SendOp49
    // / Net_SendOp50 / Net_SendOp51) sont DÉJÀ attribués à un autre flux confirmé
    // (Net_SendOp49 = réponse d'invitation d'ALLIANCE, cf.
    // Net/GameHandlers_PartyGuild.cpp ligne « Net_SendOp49(sys.Client(), 2) »).
    // Il ne faut donc PAS réutiliser ces builders ici sans preuve. Prochaine
    // étape : dump dynamique (x32dbg, cf. CLAUDE.md) du paquet émis au clic
    // « Accepter » dans le client réel pour identifier le bon Net_SendOpNN.
    Close();
}

void PlayerTradeWindow::HandleCancel() {
    // TODO(send): annuler l'échange en cours côté serveur. Même remarque que
    // HandleAccept() : aucun builder outbound confirmé pour cette action.
    // Prochaine étape : dump dynamique du paquet émis au clic « Annuler ».
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

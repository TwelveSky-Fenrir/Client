// UI/AutoPlayWindow.cpp — implémentation du panneau « AutoPlay ».
// Voir UI/AutoPlayWindow.h pour le contrat et les réserves sur les données
// affichées (pas de nom de monstre disponible, état « actif » purement local).
#include "UI/AutoPlayWindow.h"
#include "UI/PanelSkin.h"

#include <cstdio>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit (400,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec le panneau AutoPlay (240 x ~304 selon
// RecomputeLayout, ratio le plus proche parmi les gabarits identifiés ; cf.
// méthodologie détaillée dans UI/PanelSkin.h). Indice distinct de ceux
// utilisés par OptionsWindow/VendorShopWindow (même cluster). Repli
// automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_03641.IMG");
} // namespace

// ============================================================================
// Géométrie
// ============================================================================
void AutoPlayWindow::RecomputeLayout(int screenW, int screenH) {
    const int listH = kRowCount * kRowH;
    const int h = kPadY + kTitleH + kCheckH + 6 + listH + 8 + kButtonH + kPadY;
    const int w = kPanelW;
    const int px = (screenW - w) / 2;
    const int py = (screenH - h) / 2;

    panel_ = { px, py, w, h };
    // Dialog::x_/y_ : recentré chaque frame, comme les autres dialogues modaux.
    x_ = px;
    y_ = py;

    closeBtn_ = { px + w - kPadX - kCloseSize, py + (kTitleH - kCloseSize) / 2,
                  kCloseSize, kCloseSize };

    const int cy = py + kTitleH;
    checkbox_ = { px + kPadX, cy + (kCheckH - kCheckSize) / 2, kCheckSize, kCheckSize };
    checkboxLabel_ = { px + kPadX, cy, w - 2 * kPadX, kCheckH }; // case + libellé cliquables

    const int listY = cy + kCheckH + 6;
    for (int i = 0; i < kRowCount; ++i)
        rows_[i] = { px + kPadX, listY + i * kRowH, w - 2 * kPadX, kRowH };

    const int btnY = listY + listH + 8;
    clearBtn_ = { px + kPadX, btnY, w - 2 * kPadX, kButtonH };
}

// Dérive l'état d'affichage d'un slot à partir de AutoPlaySystem::Targets() +
// (si le monstre référencé est encore vivant côté g_World.monsters) ses PV.
// AUCUN nom : MONSTER_INFO n'a pas d'accesseur typé dans Game/GameDatabase.h.
AutoPlayWindow::RowView AutoPlayWindow::BuildRow(int slotIndex) const {
    RowView r;
    if (!system_) return r;

    const auto& targets = system_->Targets();
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= targets.size()) return r;

    const game::AutoPlayTargetSlot& slot = targets[static_cast<size_t>(slotIndex)];
    r.used         = slot.monsterIndex >= 0;
    r.monsterIndex = slot.monsterIndex;
    r.distance     = slot.distance;
    r.available    = slot.available;
    r.locked       = r.used && (slot.monsterIndex == system_->CurrentTargetIndex());

    if (r.used) {
        const auto& monsters = game::g_World.monsters;
        if (static_cast<size_t>(slot.monsterIndex) < monsters.size()) {
            r.hp    = monsters[static_cast<size_t>(slot.monsterIndex)].hp;
            r.hasHp = true;
        }
    }
    return r;
}

// ============================================================================
// Événements souris / clavier
// ============================================================================
bool AutoPlayWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    if (!panel_.Contains(x, y)) return false;

    closeArmed_ = closeBtn_.Contains(x, y);
    clearArmed_ = clearBtn_.Contains(x, y);

    if (checkboxLabel_.Contains(x, y)) {
        checkArmed_ = true;
        enabled_ = !enabled_; // case à cocher : bascule immédiate (comme un vrai checkbox)
        // TODO(send): AutoPlaySystem ne possède aucun drapeau "actif" côté serveur — le
        // seul vecteur de synchronisation connu pour la config auto-farm est le blob
        // 44 o "autoHunt44" du paquet opcode 0x63 : Net_SendOp99(nc, a2, appearance68,
        // &configSerialisee44o) (Net/SendPackets.h). Aucun opcode dédié "toggle autoplay"
        // identifié dans le cluster AutoPlay_* (0x457EA0..0x45D080) ; si le serveur doit
        // être notifié du changement, sérialiser AutoPlaySystem::config (44 o, layout
        // g_AutoHuntMode.. à confirmer) et appeler ce builder ici.
    } else {
        checkArmed_ = false;
    }

    return true; // clic dans le panneau : toujours consommé (règle premier-consommateur)
}

bool AutoPlayWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    const bool inPanel = panel_.Contains(x, y);

    if (closeArmed_) {
        closeArmed_ = false;
        if (closeBtn_.Contains(x, y)) {
            Close();
            return true;
        }
    }
    if (clearArmed_) {
        clearArmed_ = false;
        if (clearBtn_.Contains(x, y) && system_) {
            system_->ResetTargetList(); // AutoPlay_ResetTargetList 0x458AB0 — purement local
            return true;
        }
    }
    checkArmed_ = false;
    return inPanel;
}

bool AutoPlayWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// ============================================================================
// Rendu
// ============================================================================
void AutoPlayWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Recalcule la géométrie aux DEUX phases (résultat identique dans la même
    // frame) : nécessaire pour que la phase Text s'aligne sur les rects tout
    // juste remplis en phase Panels, et pour que le hit-test différé (routé
    // entre deux frames) reste juste après un redimensionnement écran.
    RecomputeLayout(ctx.screenW, ctx.screenH);
    if (!bOpen_) return;

    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, panel_.x, panel_.y, panel_.w, panel_.h, kColBg);
        ctx.DrawFrame(panel_.x, panel_.y, panel_.w, panel_.h, kColBorder, 1);

        // Bouton fermeture (croix), survol en surbrillance.
        const bool closeHover = closeBtn_.Contains(cursorX, cursorY);
        ctx.FillRect(closeBtn_.x, closeBtn_.y, closeBtn_.w, closeBtn_.h,
                     closeHover ? kColHover : kColButtonBg);
        ctx.DrawFrame(closeBtn_.x, closeBtn_.y, closeBtn_.w, closeBtn_.h, kColBorder, 1);

        // Case à cocher « AutoPlay actif ».
        ctx.FillRect(checkbox_.x, checkbox_.y, checkbox_.w, checkbox_.h,
                     enabled_ ? kColSuccess : kColButtonBg);
        ctx.DrawFrame(checkbox_.x, checkbox_.y, checkbox_.w, checkbox_.h, kColBorder, 1);

        // Bouton « Vider la liste ».
        const bool clearHover = clearBtn_.Contains(cursorX, cursorY);
        ctx.FillRect(clearBtn_.x, clearBtn_.y, clearBtn_.w, clearBtn_.h,
                     clearHover ? kColHover : kColButtonBg);
        ctx.DrawFrame(clearBtn_.x, clearBtn_.y, clearBtn_.w, clearBtn_.h, kColBorder, 1);
        return;
    }

    // --- Phase texte ---
    const char* title = "AutoPlay";
    ctx.Text(title, panel_.x + (panel_.w - ctx.MeasureText(title)) / 2, panel_.y + 4, kColTitle);
    ctx.Text("X", closeBtn_.x + (closeBtn_.w - ctx.MeasureText("X")) / 2, closeBtn_.y + 2, kColText);

    ctx.Text("AutoPlay actif", checkbox_.x + kCheckSize + 6, checkbox_.y - 1,
             enabled_ ? kColSuccess : kColText);

    if (!system_) {
        ctx.Text("(systeme AutoPlay non branche)", panel_.x + kPadX, rows_[0].y, kColError);
    } else {
        char buf[96];
        for (int i = 0; i < kRowCount; ++i) {
            const RowView row = BuildRow(i);
            const Rect& r = rows_[i];
            D3DCOLOR color;

            if (!row.used) {
                std::snprintf(buf, sizeof(buf), "%2d. --- libre ---", i + 1);
                color = kColDim;
            } else if (row.locked) {
                if (row.hasHp)
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f  PV=%d [VERROU]",
                                  i + 1, row.monsterIndex, row.distance, row.hp);
                else
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f [VERROU]",
                                  i + 1, row.monsterIndex, row.distance);
                color = kColTitle;
            } else if (!row.available) {
                std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f [pris]",
                              i + 1, row.monsterIndex, row.distance);
                color = kColError;
            } else {
                if (row.hasHp)
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f  PV=%d",
                                  i + 1, row.monsterIndex, row.distance, row.hp);
                else
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f",
                                  i + 1, row.monsterIndex, row.distance);
                color = kColText;
            }
            ctx.Text(buf, r.x, r.y, color);
        }
    }

    const char* clearLabel = "Vider la liste";
    ctx.Text(clearLabel, clearBtn_.x + (clearBtn_.w - ctx.MeasureText(clearLabel)) / 2,
             clearBtn_.y + (clearBtn_.h - 12) / 2, kColText);
}

} // namespace ts2::ui

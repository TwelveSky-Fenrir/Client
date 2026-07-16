// UI/AutoPlayWindow.cpp — implémentation du panneau « AutoPlay ».
// Voir UI/AutoPlayWindow.h pour le contrat et les réserves sur les données
// affichées (pas de nom de monstre disponible). Le basculement Start/Stop émet
// Net_SendOp99 (opcode 0x63) — l'état « actif » (enabled_) est le miroir sérialisé
// de g_InvDirtyEnable 0x16755AC, PAS un drapeau purement local.
#include "UI/AutoPlayWindow.h"
#include "UI/PanelSkin.h"
// Émission de la synchro auto-hunt (Net_SendOp99 0x4BD140, opcode 0x63, 125 o) : builder
// EXISTANT net::Net_SendAutoHuntSync (Net/SendPackets.h:269) + singleton g_NetClient
// 0x8156A0 restauré par net::GlobalNetClient() (Net/NetClient.h:67-68). Messages système via
// game::g_Client.msg (Msg_AppendSystemLine 0x68D9D0) + game::Str (StrTable005_Get 0x4C1D10).
// NB : Net/SendPackets.h inclut déjà NetClient.h (même ordre winsock que CharacterStatsWindow.cpp).
#include "Net/SendPackets.h"
#include "Game/ClientRuntime.h"

#include <cstdio>
#include <cstring>

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

// ---------------------------------------------------------------------------
// Sérialisation du blob config auto-hunt 44 o émis par Op99 (offset paquet +81 ; orig.
// région contiguë g_AutoHuntMode 0x16755F4..0x1675620). CHAMP PAR CHAMP aux offsets fil
// PROUVÉS (RE Net_SendOp99 0x4BD140) : AutoPlayConfig n'est PAS compatible fil (ordre
// différent — skillAoE à +8 côté struct vs +12 fil ; bool au lieu d'int32 ; 3 champs fil
// absents) -> memcpy(&config, 44) INTERDIT. Chaque champ = int32 LE (x86 natif).
// Offset indiqué = position DANS le blob (offset paquet correspondant = +81 + offset).
// ---------------------------------------------------------------------------
void BuildAutoHunt44(const game::AutoPlayConfig& c, uint8_t out[44]) {
    int32_t w[11] = {0};
    w[0]  = c.mode;                                // +0  (pkt+81)  g_AutoHuntMode            0x16755F4
    w[1]  = static_cast<int32_t>(c.skillSingle);   // +4  (pkt+85)  g_AutoHuntSkillSingle     0x16755F8
    w[2]  = 0; // +8  (pkt+89)  g_AutoHuntSkillSingleOn 0x16755FC — absent d'AutoPlayConfig
    w[3]  = static_cast<int32_t>(c.skillAoE);      // +12 (pkt+93)  g_AutoHuntSkillAoE        0x1675600
    w[4]  = 0; // +16 (pkt+97)  g_AutoHuntSkillAoEOn   0x1675604 — absent d'AutoPlayConfig
    w[5]  = c.aoeThreshold;                        // +20 (pkt+101) g_AutoHuntAoEThreshold    0x1675608
    w[6]  = static_cast<int32_t>(c.pkFactionMask); // +24 (pkt+105) g_AutoHuntPkFactionMask   0x167560C
    w[7]  = c.warpOnStuck ? 1 : 0;                 // +28 (pkt+109) g_AutoHuntBagFullReturn   0x1675610
    w[8]  = 0; // +32 (pkt+113) g_AutoHuntSettingsDirty 0x1675614 — absent d'AutoPlayConfig
    w[9]  = c.useReturnScroll ? 1 : 0;             // +36 (pkt+117) g_AutoHuntUseReturnScroll 0x1675618
    w[10] = c.useTownItem ? 1 : 0;                 // +40 (pkt+121) g_AutoHuntUseTownItem     0x167561C
    // TODO [0x16755FC / 0x1675604 / 0x1675614] : skillSingleOn / skillAoEOn / settingsDirty
    //   non modélisés dans AutoPlayConfig (Game/AutoPlaySystem.h, non possédé par cette vague)
    //   -> émis à 0 (défaut fidèle « non configuré », PAS une invention). À modéliser côté
    //   AutoPlaySystem pour une fidélité fil complète.
    std::memcpy(out, w, 44);
}

// ---------------------------------------------------------------------------
// EmitAutoHuntSync — émet Net_SendOp99 (opcode 0x63, 125 o) via le g_NetClient global.
// Le binaire émet INCONDITIONNELLEMENT depuis &g_AutoPlayMgr (tampon global, PAS un
// manager autoplay) sans recevoir la socket ; côté C++ on lit le singleton restauré
// net::GlobalNetClient() (0x8156A0).
// ---------------------------------------------------------------------------
void EmitAutoHuntSync(game::AutoPlaySystem& sys, net::NetClient* nc, int8_t stateFlag) {
    if (!nc) return; // hors session : pas d'envoi. En scène 6 nc est non nul (PAS de code mort).

    // Blob apparence/quick-skills 68 o : byte_16755B0 (u32) + g_AutoHuntQuickSkills 0x16755B4
    // (8×{id:u32, on:u32}). AUCUN modèle C++ : écrit par UI_QuickSlot_AssignHotkey 0x5BDF00,
    // front NON possédé par cette vague. Hébergé dans le blob long-traîne partagé de
    // ClientRuntime (clé = adresse d'origine 0x16755B0) — zéros tant que le front quickslot
    // ne l'alimente pas = état « aucun quick-skill assigné » (défaut fidèle, PAS une invention).
    // TODO [0x16755B4 / UI_QuickSlot_AssignHotkey 0x5BDF00] : brancher le vrai contenu 68 o
    //   (débloque aussi les TODO(net/state) de Game/MapWarp.cpp:179/205).
    const void* appearance68 = game::g_Client.Blob(0x16755B0, 68).data();

    uint8_t autoHunt44[44];
    BuildAutoHunt44(sys.config, autoHunt44);

    // Net_SendAutoHuntSync = Net_SendOp99 0x4BD140. stateFlag émis sur 4 octets LE
    // (WriteChar4LE, piège thiscall prouvé 0x4BD1DF -> le serveur lit un int32).
    net::Net_SendAutoHuntSync(*nc, stateFlag, appearance68, autoHunt44);
}
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

    // Armement des latches au clic-ENFONCÉ uniquement (le binaire pose *this/*(this+1) en
    // mouse-down, cf. AutoPlay_OnMouseUpMain 0x45A9BE/0x45AB69) ; l'ACTION (bascule Start/Stop
    // + émission Op99) est DIFFÉRÉE au relâchement (OnClick). PAS d'effet optimiste ici :
    // l'ancien `enabled_ = !enabled_` en mouse-down était le défaut « effet local que le
    // binaire ne fait pas » (même famille que le défaut EnchantWindow de la passe 3) -> retiré.
    closeArmed_ = closeBtn_.Contains(x, y);
    clearArmed_ = clearBtn_.Contains(x, y);
    checkArmed_ = checkboxLabel_.Contains(x, y);

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
    if (checkArmed_) {
        checkArmed_ = false;
        if (checkboxLabel_.Contains(x, y)) {
            // Bascule Start/Stop émise au RELÂCHEMENT, fidèle à AutoPlay_OnMouseUpMain
            // 0x45A980 (le binaire bascule + émet en mouse-up, pas en mouse-down).
            ToggleAutoHunt();
            return true;
        }
    }
    return inPanel;
}

// ============================================================================
// Bascule Start/Stop de l'auto-hunt — émission Net_SendOp99 (opcode 0x63).
// Miroir fidèle de AutoPlay_OnMouseUpMain 0x45A980 (branche START unk_9647F8 /
// branche STOP unk_964920). La case à cocher unique de ce portage joue les deux
// rôles : START si l'auto-hunt est arrêté (enabled_ == false), STOP sinon.
// ============================================================================
void AutoPlayWindow::ToggleAutoHunt() {
    if (!system_) return; // le binaire a toujours une machine d'état ; sans elle, rien à piloter

    net::NetClient* nc = net::GlobalNetClient(); // &g_NetClient 0x8156A0 (non nul en scène 6)

    // enabled_ = miroir de g_InvDirtyEnable 0x16755AC (drapeau maître 0/1). La garde START du
    // binaire est `if (!g_InvDirtyEnable)` 0x45AA7D : on ne (re)démarre que si arrêté.
    if (!enabled_) {
        // ===== START (bouton "Start" unk_9647F8, 0x45A9BE) =====
        // Garde a (0x45AA01) : dword_1673248 <= 0 || !AutoPlay_HasPotionsSet() -> refus 1790.
        //   dword_1673248 = externalState.classItemId (« cœur de classe » équipé).
        //   AutoPlay_HasPotionsSet 0x45E700 : NON modélisé dans AutoPlaySystem -> garde
        //   partielle (seule la moitié classItemId est reproduite).
        // NB : classItemId est un uint32_t côté C++ ; le binaire compare dword_1673248 en
        // SIGNÉ (`<= 0`), d'où le cast int32_t (reproduit le compare signé, évite aussi le
        // warning « unsigned <= 0 toujours == 0 »).
        if (static_cast<int32_t>(system_->externalState.classItemId) <= 0
            /* TODO [0x45E700] : && AutoPlay_HasPotionsSet() — état potions non modélisé */) {
            game::g_Client.msg.System(game::Str(1790)); // Msg_AppendSystemLine 1790 (0x45A9E4)
            return;                                       // 0x45A9F9 : AUCUNE émission
        }
        // Garde b (0x45AA38) : !AutoPlay_HasRequiredItems() -> refus 1792.
        if (!system_->HasRequiredItems()) {              // AutoPlay_HasRequiredItems 0x45CC10
            game::g_Client.msg.System(game::Str(1792)); // Msg_AppendSystemLine 1792 (0x45AA52)
            return;                                       // 0x45AA67 : AUCUNE émission
        }
        // 0x45AA6C : dword_1675B20 = -1 (reset du slot de parchemin auto en vol). État PRIVÉ
        // d'AutoPlaySystem (pendingItemUseSlot_) sans setter public exposé.
        // TODO [0x1675B20] : reset non reproductible sans API sur AutoPlaySystem (non possédé).

        // Garde c (0x45AA8C) : !AutoPlay_IsNpcTargetable(g_SelfMorphNpcId) -> refus 2418.
        //   AutoPlay_IsNpcTargetable 0x45FD90 / g_SelfMorphNpcId 0x1675A98 : NON modélisés.
        // TODO [0x45FD90 / 0x1675A98] : garde IsNpcTargetable non reproduite (état non modélisé)
        //   -> on émet directement (le binaire émet aussi lorsque la cible morph est valide).

        enabled_ = true;                               // g_InvDirtyEnable = 1 (0x45AAC0)
        system_->externalState.invDirtyEnable = true;  // write-through vers le miroir gameplay
        EmitAutoHuntSync(*system_, nc, 1);             // Net_SendOp99(1) (0x45AAD1)
        // 0x45AADB..0x45AB1F : cQuickSlotWin_Close / this[20]=0 / unfocus editbox / this[71]=0
        //   = état d'autres fenêtres (quickslots / editbox), hors de ce widget.
        // TODO [cQuickSlotWin_Close 0x65F5A0] : fermeture des quickslots non reproduite ici.
        game::g_Client.msg.System(game::Str(1907));    // Msg_AppendSystemLine 1907 (0x45AB0C)
    } else {
        // ===== STOP (bouton "Stop" unk_964920, 0x45AB69) : émission INCONDITIONNELLE =====
        enabled_ = false;                              // g_InvDirtyEnable = 0 (0x45AB72)
        system_->externalState.invDirtyEnable = false; // write-through
        EmitAutoHuntSync(*system_, nc, 0);             // Net_SendOp99(0) (0x45AB88)
        system_->ResetTargetList();                    // AutoPlay_ResetTargetList 0x458AB0 (0x45AB90)
        game::g_Client.msg.System(game::Str(1908));    // Msg_AppendSystemLine 1908 (0x45ABA5)
    }
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

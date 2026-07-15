// UI/GuildWindow.cpp — implémentation de la fenêtre « Guilde ». Voir UI/GuildWindow.h.
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant
// <windows.h>, qu'UI/GuildWindow.h tire transitivement via <d3d9.h>) — même
// convention que UI/ChatWindow.cpp / UI/WarehouseWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 puis windows (ordre sur)
#include "Net/NetClient.h"
#include "UI/GuildWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameState.h" // game::g_World.allianceRoster.guildName (titre réel)

#include <algorithm>
#include <cstring>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit (446,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec le panneau Guilde (300x284, cf. méthodologie
// détaillée dans UI/PanelSkin.h). Repli automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01165.IMG");

// Palette (cf. contrat de mission — D3DCOLOR = 0xAARRGGBB) : fond panneau ~0xE0202028,
// cadre ~0xFF808080, texte ~0xFFFFFFFF, titre ~0xFFFFDD66, survol ~0xFF4060A0,
// erreur ~0xFFFF6060, succès ~0xFF60FF60.
constexpr D3DCOLOR kColBg        = Argb(224,  32,  32,  40);
constexpr D3DCOLOR kColBorder    = Argb(255, 128, 128, 128);
constexpr D3DCOLOR kColHeaderBg  = Argb(255,  40,  40,  56);
constexpr D3DCOLOR kColText      = Argb(255, 255, 255, 255);
constexpr D3DCOLOR kColTitle     = Argb(255, 255, 221, 102);
constexpr D3DCOLOR kColSubtle    = Argb(200, 200, 200, 200);
constexpr D3DCOLOR kColBtn       = Argb(255,  56,  64,  88);
constexpr D3DCOLOR kColHover     = Argb(255,  64,  96, 160);
constexpr D3DCOLOR kColRowAlt    = Argb( 40, 255, 255, 255);
constexpr D3DCOLOR kColError     = Argb(255, 255,  96,  96);
constexpr D3DCOLOR kColErrorHov  = Argb(255, 255, 140, 140);
constexpr D3DCOLOR kColSuccess   = Argb(255,  96, 255,  96);
constexpr D3DCOLOR kColSuccessHv = Argb(255, 140, 255, 140);
constexpr D3DCOLOR kColEditBg    = Argb(255,  20,  20,  28);
constexpr D3DCOLOR kColEditFocus = Argb(255,  40,  40,  64);
} // namespace

GuildWindow::GuildWindow() {
    // Longueur max du nom = kNameStride - 1 (NUL-terminé, cf. GuildSystem.h).
    nameEdit_.SetMaxLength(static_cast<size_t>(game::GuildRoster::kNameStride - 1));
    nameEdit_.SetTextColor(kColText);
    nameEdit_.SetCaretColor(kColText);
}

// ============================================================================
// Cycle de vie
// ============================================================================
void GuildWindow::Open() {
    Dialog::Open();
    addMode_        = false;
    scrollOffset_   = 0;
    pressedBtn_      = PressedBtn::None;
    pressedKickIdx_  = -1;
    feedback_.clear();
    feedbackUntil_  = -1.0f;
    nameEdit_.Clear();
    nameEdit_.SetFocused(false);
}

void GuildWindow::Close() {
    Dialog::Close();
    addMode_ = false;
    nameEdit_.SetFocused(false);
}

// ============================================================================
// Géométrie
// ============================================================================
// RE-VÉRIFIÉ par décompilation fraîche (audit positions fenêtres, 2026-07-14) :
// UI_ClanWin_Draw 0x5DA210 (la vraie fenêtre Guilde/Clan d'origine) recentre elle
// aussi SA position à CHAQUE frame avec exactement cette formule :
//   *this     = nWidth/2  - Sprite2D_GetWidth(bg)/2   (dword_1669184 = nWidth)
//   *(this+1) = nHeight/2 - Sprite2D_GetHeight(bg)/2  (dword_1669188 = nHeight)
// où nWidth/nHeight sont la résolution ÉCRAN COURANTE (pas une résolution de
// référence figée 1024x768) -> AUCUN facteur d'échelle/offset parasite : la formule
// `screenW/2 - kPanelW/2` ci-dessous est donc bit-exacte au binaire. Seules les
// dimensions kPanelW/kPanelH restent une approximation (la vraie largeur/hauteur du
// sprite de fond ne sont pas des constantes statiques : elles sont lues au runtime
// aux offsets +108/+112 d'un Sprite2D chargé paresseusement depuis un .IMG absent de
// l'IDB désassemblée, cf. Sprite2D_GetWidth/GetHeight 0x4D6CD0/0x4D6D20).
// NOTE ARCHITECTURE (hors scope de cette correction) : la VRAIE UI_ClanWin_Draw est
// une bande de 7 boutons-icônes verticaux (x+12, y+28/54/80/106/132/158/184) ouvrant
// des sous-panneaux, PAS la liste membres + Ajouter/Expulser dessinée ici — cette
// dernière est une réinvention pragmatique (roster via GuildSystem.h) qui NE
// correspond PAS 1:1 à la disposition d'origine ; seul le CENTRAGE écran est prouvé
// identique.
// COMPLÉMENT (re-décompilation 2026-07-14, seconde passe d'audit) : le VRAI panneau
// « liste des membres » ouvert par un des 7 boutons ci-dessus a été localisé :
// UI_GuildMgrWnd_Open/Render 0x667E20/0x66A2E0 (état à g_Guild == dword_1839968,
// PAS la même instance que le roster local `this` du code ci-dessous). Il utilise
// EXACTEMENT la même formule de centrage (`nWidth/2 - Sprite2D_GetWidth/2`,
// `nHeight/2 - Sprite2D_GetHeight/2`, cf. Guild_CountMembers/Guild_SelectNextMember
// 0x66BBC0/0x66BC30) — reconfirme le centrage. Structure réelle, pour référence
// future si la disposition doit être rapprochée du binaire : 10 lignes visibles par
// PAGE (`*(this+427)` = page 0..4) sur 50 slots (5 pages de 10, PAS un scroll continu
// comme ici), colonnes nom (x+110) / statut connecté-déconnecté (x+220, StrTable005
// #463/464/465) / rang-titre (x+340, StrTable003), sélecteur de ligne surligné à
// (x+17, y+20*i+190). Sous-panneau « inviter » = case 2 du switch sur `*(this+426)` :
// un SEUL edit box (nom, x+16/y+43) + 2 boutons (x+272/x+331, y+71) — PAS de bouton
// « expulser » par ligne visible dans ce désassemblage (aucune preuve d'un mécanisme
// de kick per-row ; le bouton « X » par ligne dessiné ici reste une invention
// pragmatique). Non reporté sur la géométrie ci-dessous (paging vs scroll, colonnes
// différentes) faute de mandat pour une réécriture 1:1 de la disposition — seul le
// centrage était en cause dans cette mission et il est déjà bit-exact.
GuildWindow::Geom GuildWindow::ComputeGeometry(int screenW, int screenH) const {
    Geom g;
    g.panel = { screenW / 2 - kPanelW / 2, screenH / 2 - kPanelH / 2, kPanelW, kPanelH };
    g.header = { g.panel.x, g.panel.y, kPanelW, kHeaderH };
    g.closeBtn = { g.panel.x + kPanelW - kCloseBtnSize - 6,
                   g.panel.y + (kHeaderH - kCloseBtnSize) / 2,
                   kCloseBtnSize, kCloseBtnSize };

    g.listArea = { g.panel.x + kMargin, g.panel.y + kHeaderH + kListGap,
                   kPanelW - 2 * kMargin - kScrollBtnSize - 4, kVisibleRows * kRowH };
    g.scrollUp   = { g.listArea.x + g.listArea.w + 4, g.listArea.y,
                     kScrollBtnSize, kScrollBtnSize };
    g.scrollDown = { g.listArea.x + g.listArea.w + 4,
                     g.listArea.y + g.listArea.h - kScrollBtnSize,
                     kScrollBtnSize, kScrollBtnSize };

    g.actionRow = { g.panel.x + kMargin, g.listArea.y + g.listArea.h + kListGap,
                    kPanelW - 2 * kMargin, kActionH };

    if (addMode_) {
        g.editBox    = { g.actionRow.x, g.actionRow.y, 130, kActionH };
        g.confirmBtn = { g.editBox.x + g.editBox.w + 6, g.actionRow.y, 64, kActionH };
        g.cancelBtn  = { g.confirmBtn.x + g.confirmBtn.w + 6, g.actionRow.y, 56, kActionH };
        g.addBtn     = { 0, 0, 0, 0 };
    } else {
        g.addBtn     = { g.actionRow.x, g.actionRow.y, 110, kActionH };
        g.editBox = g.confirmBtn = g.cancelBtn = Rect{ 0, 0, 0, 0 };
    }

    g.feedbackArea = { g.panel.x + kMargin, g.actionRow.y + kActionH + 4,
                       kPanelW - 2 * kMargin, kFeedbackH };
    return g;
}

GuildWindow::Rect GuildWindow::RowRect(const Geom& g, int rowOnScreen) const {
    return { g.listArea.x, g.listArea.y + rowOnScreen * kRowH, g.listArea.w, kRowH };
}

GuildWindow::Rect GuildWindow::KickRect(const Geom& g, int rowOnScreen) const {
    const Rect r = RowRect(g, rowOnScreen);
    return { r.x + r.w - kKickBtnSize - 2, r.y + (kRowH - kKickBtnSize) / 2,
             kKickBtnSize, kKickBtnSize };
}

std::vector<int> GuildWindow::VisibleMemberIndices() const {
    std::vector<int> out;
    out.reserve(game::GuildRoster::kMaxMembers);
    for (int i = 0; i < game::GuildRoster::kMaxMembers; ++i)
        if (!game::g_Guild.members[static_cast<size_t>(i)].Empty())
            out.push_back(i);
    return out;
}

int GuildWindow::MaxScroll() const {
    const int n = static_cast<int>(VisibleMemberIndices().size());
    return std::max(0, n - kVisibleRows);
}

// ============================================================================
// Ajout / expulsion
// ============================================================================
void GuildWindow::ConfirmAdd() {
    const std::string name = nameEdit_.Text();
    if (name.empty()) {
        feedback_      = "Nom vide.";
        feedbackColor_ = kColError;
        feedbackUntil_ = lastGameTimeSec_ + kFeedbackDurationSec;
        return; // reste en mode saisie
    }

    // Dictionnaire de mots bannis (maybe_Dict001_MatchWord 0x4C1410) hors périmètre de
    // GuildSystem.h -> banned toujours faux ici (cf. GuildRoster::AddMember, commentaire
    // "banned est calculé par l'appelant").
    const bool banned = false;
    const bool ok = game::g_Guild.AddMember(name, banned);

    if (ok) {
        // Net_SendOp76(nc, name61) — Net/SendPackets.h:108 (opcode sortant 0x4C, payload
        // brut 61 o). Confirmé par décompilation IDA de Guild_AddMemberFromInput 0x66BCD0
        // (seul appelant d'origine, xref unique 0x66bd5b) : le nom lu dans l'edit-box est
        // copié tel quel (NUL-rempli) dans le buffer envoyé, sans structure additionnelle.
        // Le roster n'est mis à jour qu'à la réponse serveur (Pkt_GuildMemberInfo 0x37 /
        // GuildInfoUpdate 0x38), donc on NE modifie PAS members[] ici — fidèle à
        // Guild_AddMemberFromInput qui se contente de décider l'envoi. net_ nullable
        // (cf. Bind(), UI/GuildWindow.h) : no-op silencieux tant que non lié.
        if (net_) {
            char name61[61] = {};
            const size_t n = std::min(name.size(), sizeof(name61) - 1);
            std::memcpy(name61, name.data(), n);
            net::Net_SendOp76(*net_, name61);
            feedback_ = "Demande d'ajout envoyee : " + name;
        } else {
            feedback_ = "Demande d'ajout preparee (reseau non lie) : " + name;
        }
        feedbackColor_ = kColSuccess;
    } else {
        feedback_      = "Nom rejete (banni).";
        feedbackColor_ = kColError;
    }
    feedbackUntil_ = lastGameTimeSec_ + kFeedbackDurationSec;

    addMode_ = false;
    nameEdit_.Clear();
    nameEdit_.SetFocused(false);
}

void GuildWindow::CancelAdd() {
    addMode_ = false;
    nameEdit_.Clear();
    nameEdit_.SetFocused(false);
}

// ============================================================================
// Événements souris (latch armé au down, validé au relâchement — pattern MsgBoxDialog)
// ============================================================================
bool GuildWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    const Geom g = ComputeGeometry(lastScreenW_, lastScreenH_);
    pressedBtn_     = PressedBtn::None;
    pressedKickIdx_ = -1;

    if (PointInRect(x, y, g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h)) {
        pressedBtn_ = PressedBtn::Close;
    } else if (addMode_) {
        if (PointInRect(x, y, g.editBox.x, g.editBox.y, g.editBox.w, g.editBox.h)) {
            nameEdit_.SetFocused(true);
        } else if (PointInRect(x, y, g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h)) {
            pressedBtn_ = PressedBtn::Confirm;
        } else if (PointInRect(x, y, g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h)) {
            pressedBtn_ = PressedBtn::Cancel;
        } else {
            nameEdit_.SetFocused(false);
        }
    } else if (PointInRect(x, y, g.addBtn.x, g.addBtn.y, g.addBtn.w, g.addBtn.h)) {
        pressedBtn_ = PressedBtn::Add;
    } else if (PointInRect(x, y, g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h)) {
        pressedBtn_ = PressedBtn::ScrollUp;
    } else if (PointInRect(x, y, g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h)) {
        pressedBtn_ = PressedBtn::ScrollDown;
    } else {
        const std::vector<int> visible = VisibleMemberIndices();
        const int rows = std::min(kVisibleRows,
                                   static_cast<int>(visible.size()) - scrollOffset_);
        for (int i = 0; i < rows; ++i) {
            const Rect kr = KickRect(g, i);
            if (PointInRect(x, y, kr.x, kr.y, kr.w, kr.h)) {
                pressedBtn_     = PressedBtn::Kick;
                pressedKickIdx_ = visible[static_cast<size_t>(scrollOffset_ + i)];
                break;
            }
        }
    }

    return PointInRect(x, y, g.panel.x, g.panel.y, g.panel.w, g.panel.h);
}

bool GuildWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    const Geom g = ComputeGeometry(lastScreenW_, lastScreenH_);
    const bool consumed = PointInRect(x, y, g.panel.x, g.panel.y, g.panel.w, g.panel.h);

    switch (pressedBtn_) {
        case PressedBtn::Close:
            if (PointInRect(x, y, g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h))
                Close();
            break;

        case PressedBtn::Add:
            if (PointInRect(x, y, g.addBtn.x, g.addBtn.y, g.addBtn.w, g.addBtn.h)) {
                addMode_ = true;
                nameEdit_.Clear();
                nameEdit_.SetFocused(true);
                feedback_.clear();
                feedbackUntil_ = -1.0f;
            }
            break;

        case PressedBtn::ScrollUp:
            if (PointInRect(x, y, g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h))
                scrollOffset_ = std::max(0, scrollOffset_ - 1);
            break;

        case PressedBtn::ScrollDown:
            if (PointInRect(x, y, g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h))
                scrollOffset_ = std::min(MaxScroll(), scrollOffset_ + 1);
            break;

        case PressedBtn::Confirm:
            if (PointInRect(x, y, g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h))
                ConfirmAdd();
            break;

        case PressedBtn::Cancel:
            if (PointInRect(x, y, g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h))
                CancelAdd();
            break;

        case PressedBtn::Kick: {
            const std::vector<int> visible = VisibleMemberIndices();
            const int rows = std::min(kVisibleRows,
                                       static_cast<int>(visible.size()) - scrollOffset_);
            for (int i = 0; i < rows; ++i) {
                if (visible[static_cast<size_t>(scrollOffset_ + i)] != pressedKickIdx_) continue;
                const Rect kr = KickRect(g, i);
                if (PointInRect(x, y, kr.x, kr.y, kr.w, kr.h)) {
                    // TODO(send): opcode d'expulsion de guilde NON identifié dans
                    // Net/SendPackets.h — aucun builder Net_SendOpNN documenté pour une
                    // demande de kick émise par le client. Seul le chemin ENTRANT est
                    // connu (Net_OnTeamFormationDispatch 0x491E70 case 8, cf.
                    // GuildSystem.h). Ne pas inventer l'opcode : à brancher depuis
                    // Net/GameHandlers_PartyGuild.cpp une fois le builder identifié
                    // (x32dbg ou xrefs statiques idaTs2). Mutation locale immédiate
                    // demandée par la mission, malgré l'absence d'envoi réel.
                    game::g_Guild.RemoveMember(pressedKickIdx_);
                    feedback_      = "Membre retire (local, TODO net) : #" +
                                     std::to_string(pressedKickIdx_);
                    feedbackColor_ = kColError;
                    feedbackUntil_ = lastGameTimeSec_ + kFeedbackDurationSec;
                }
                break;
            }
            break;
        }

        default: break;
    }

    pressedBtn_     = PressedBtn::None;
    pressedKickIdx_ = -1;
    return consumed;
}

// ============================================================================
// Clavier — voir la limitation documentée en tête de UI/GuildWindow.h (pas de WM_CHAR
// routé par l'UIManager, seulement OnKey(vk) = WM_KEYDOWN).
// ============================================================================
bool GuildWindow::OnKey(int vk) {
    if (!bOpen_) return false;

    if (addMode_ && nameEdit_.Focused()) {
        if (vk == VK_RETURN) { ConfirmAdd(); return true; }
        if (vk == VK_ESCAPE) { CancelAdd();  return true; }
        if (vk == VK_BACK || vk == VK_DELETE || vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_HOME || vk == VK_END) {
            nameEdit_.OnKey(vk);
            return true;
        }
        // VK_0..VK_9 (0x30-0x39) et VK_A..VK_Z (0x41-0x5A) coïncident avec leurs codes
        // ASCII en Win32 -> saisie basique majuscules/chiffres/espace uniquement.
        if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || vk == VK_SPACE) {
            nameEdit_.OnChar(static_cast<unsigned int>(vk));
            return true;
        }
        return true; // saisie modale : capte le reste du clavier sans effet
    }

    if (vk == VK_ESCAPE) { Close(); return true; }
    return false;
}

// ============================================================================
// Rendu
// ============================================================================
void GuildWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_     = ctx.screenW;
    lastScreenH_     = ctx.screenH;
    lastGameTimeSec_ = ctx.gameTimeSec;

    if (feedbackUntil_ > 0.0f && ctx.gameTimeSec >= feedbackUntil_) {
        feedback_.clear();
        feedbackUntil_ = -1.0f;
    }
    if (!bOpen_) return;

    const Geom g = ComputeGeometry(ctx.screenW, ctx.screenH);
    const std::vector<int> visible = VisibleMemberIndices();
    const int rows = std::min(kVisibleRows, static_cast<int>(visible.size()) - scrollOffset_);

    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, g.panel.x, g.panel.y, g.panel.w, g.panel.h, kColBg);
        ctx.DrawFrame(g.panel.x, g.panel.y, g.panel.w, g.panel.h, kColBorder, 2);
        ctx.FillRect(g.header.x, g.header.y, g.header.w, g.header.h, kColHeaderBg);

        const bool closeHover = PointInRect(cursorX, cursorY, g.closeBtn.x, g.closeBtn.y,
                                             g.closeBtn.w, g.closeBtn.h);
        ctx.FillRect(g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h,
                     closeHover ? kColHover : kColBtn);
        ctx.DrawFrame(g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h, kColBorder, 1);

        ctx.DrawFrame(g.listArea.x, g.listArea.y, g.listArea.w, g.listArea.h, kColBorder, 1);

        for (int i = 0; i < rows; ++i) {
            const Rect r = RowRect(g, i);
            const bool hovered = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
            if (hovered) ctx.FillRect(r.x, r.y, r.w, r.h, kColHover);
            else if ((i % 2) == 1) ctx.FillRect(r.x, r.y, r.w, r.h, kColRowAlt);

            const Rect kr = KickRect(g, i);
            const bool kickHover = PointInRect(cursorX, cursorY, kr.x, kr.y, kr.w, kr.h);
            ctx.FillRect(kr.x, kr.y, kr.w, kr.h, kickHover ? kColErrorHov : kColError);
            ctx.DrawFrame(kr.x, kr.y, kr.w, kr.h, kColBorder, 1);
        }

        const bool upHover = PointInRect(cursorX, cursorY, g.scrollUp.x, g.scrollUp.y,
                                          g.scrollUp.w, g.scrollUp.h);
        ctx.FillRect(g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h,
                     upHover ? kColHover : kColBtn);
        ctx.DrawFrame(g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h, kColBorder, 1);

        const bool downHover = PointInRect(cursorX, cursorY, g.scrollDown.x, g.scrollDown.y,
                                            g.scrollDown.w, g.scrollDown.h);
        ctx.FillRect(g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h,
                     downHover ? kColHover : kColBtn);
        ctx.DrawFrame(g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h, kColBorder, 1);

        if (addMode_) {
            ctx.FillRect(g.editBox.x, g.editBox.y, g.editBox.w, g.editBox.h,
                         nameEdit_.Focused() ? kColEditFocus : kColEditBg);
            ctx.DrawFrame(g.editBox.x, g.editBox.y, g.editBox.w, g.editBox.h, kColBorder, 1);

            const bool confirmHover = PointInRect(cursorX, cursorY, g.confirmBtn.x, g.confirmBtn.y,
                                                   g.confirmBtn.w, g.confirmBtn.h);
            ctx.FillRect(g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h,
                         confirmHover ? kColSuccessHv : kColSuccess);
            ctx.DrawFrame(g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h, kColBorder, 1);

            const bool cancelHover = PointInRect(cursorX, cursorY, g.cancelBtn.x, g.cancelBtn.y,
                                                  g.cancelBtn.w, g.cancelBtn.h);
            ctx.FillRect(g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h,
                         cancelHover ? kColHover : kColBtn);
            ctx.DrawFrame(g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h, kColBorder, 1);
        } else {
            const bool addHover = PointInRect(cursorX, cursorY, g.addBtn.x, g.addBtn.y,
                                               g.addBtn.w, g.addBtn.h);
            ctx.FillRect(g.addBtn.x, g.addBtn.y, g.addBtn.w, g.addBtn.h,
                         addHover ? kColHover : kColBtn);
            ctx.DrawFrame(g.addBtn.x, g.addBtn.y, g.addBtn.w, g.addBtn.h, kColBorder, 1);
        }
        return;
    }

    // --- Phase texte ---
    // Titre : nom RÉEL de la guilde active (game::g_World.allianceRoster.guildName ==
    // g_LocalGuildName 0x168740C, cf. Game/GameState.h::AllianceRoster et Docs/
    // TS2_ALLIANCE_PARTY_ROSTER.md §3) — DISTINCT du roster interne à 50 membres (g_Guild)
    // géré par cette fenêtre. "Guilde" seul tant qu'aucun handler n'a encore peuplé le nom
    // (avant Net_OnGuildRosterReset/Update, ou hors guilde) — dégradation honnête, pas
    // d'invention.
    const std::string& liveGuildName = game::g_World.allianceRoster.guildName;
    const std::string title = liveGuildName.empty() ? std::string("Guilde")
                                                      : ("Guilde : " + liveGuildName);
    ctx.Text(title.c_str(), g.panel.x + 10, g.panel.y + 7, kColTitle);

    const std::string count = std::to_string(game::g_Guild.CountMembers()) + "/" +
                               std::to_string(game::GuildRoster::kMaxMembers) + " membres";
    const int countW = ctx.MeasureText(count.c_str());
    ctx.Text(count.c_str(), g.closeBtn.x - countW - 10, g.panel.y + 7, kColText);
    ctx.Text("X", g.closeBtn.x + 5, g.closeBtn.y + 2, kColText);

    if (rows == 0) {
        ctx.Text("(aucun membre)", g.listArea.x + 4, g.listArea.y + 4, kColSubtle);
    }
    for (int i = 0; i < rows; ++i) {
        const Rect r = RowRect(g, i);
        const int memberIdx = visible[static_cast<size_t>(scrollOffset_ + i)];
        const game::GuildMember& m = game::g_Guild.members[static_cast<size_t>(memberIdx)];

        ctx.Text(m.name.c_str(), r.x + 4, r.y + 2, kColText);

        const std::string rankStr = "Rang " + std::to_string(m.rank);
        const int rankW = ctx.MeasureText(rankStr.c_str());
        ctx.Text(rankStr.c_str(), r.x + r.w - rankW - kKickBtnSize - 10, r.y + 2, kColSubtle);

        const Rect kr = KickRect(g, i);
        ctx.Text("X", kr.x + 4, kr.y + 1, kColText);
    }

    ctx.Text(scrollOffset_ > 0 ? "^" : "-", g.scrollUp.x + 5, g.scrollUp.y + 1, kColText);
    ctx.Text(scrollOffset_ < MaxScroll() ? "v" : "-", g.scrollDown.x + 5, g.scrollDown.y + 1, kColText);

    if (addMode_) {
        ctx.Text(nameEdit_.Text().c_str(), g.editBox.x + 4, g.editBox.y + 6, kColText);
        // Caret clignotant simplifié : collé en fin de texte (pas de suivi de position
        // médiane, EditBox::caret_ étant privé — cf. limitation documentée en en-tête).
        if (nameEdit_.Focused() && (static_cast<int>(ctx.gameTimeSec * 2.0f) & 1) == 0) {
            const int caretX = g.editBox.x + 4 + ctx.MeasureText(nameEdit_.Text().c_str());
            ctx.Text("|", caretX, g.editBox.y + 6, kColText);
        }
        ctx.Text("Confirmer", g.confirmBtn.x + 4, g.confirmBtn.y + 6, kColText);
        ctx.Text("Annuler", g.cancelBtn.x + 6, g.cancelBtn.y + 6, kColText);
    } else {
        ctx.Text("Ajouter", g.addBtn.x + 10, g.addBtn.y + 6, kColText);
    }

    if (!feedback_.empty())
        ctx.Text(feedback_.c_str(), g.feedbackArea.x, g.feedbackArea.y, feedbackColor_);
}

} // namespace ts2::ui

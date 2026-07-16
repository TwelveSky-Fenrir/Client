// UI/GuildWindow.cpp — implémentation de la fenêtre « Guilde ». Voir UI/GuildWindow.h.
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant
// <windows.h>, qu'UI/GuildWindow.h tire transitivement via <d3d9.h>) — même
// convention que UI/ChatWindow.cpp / UI/WarehouseWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 puis windows (ordre sur)
#include "Net/NetClient.h"     // net::GlobalNetClient() — singleton g_NetClient 0x8156A0
#include "UI/GuildWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameState.h"     // game::g_World.self.localPlayerName / allianceRoster.guildName
#include "Game/ClientRuntime.h" // game::g_Client.msg.System + game::Str (StrTable005_Get)

#include <algorithm>
#include <cstring>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit (446,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec le panneau Guilde (300x284, cf. méthodologie
// détaillée dans UI/PanelSkin.h). Repli automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01165.IMG");

// Palette (cf. contrat de mission — D3DCOLOR = 0xAARRGGBB).
constexpr D3DCOLOR kColBg        = Argb(224,  32,  32,  40);
constexpr D3DCOLOR kColBorder    = Argb(255, 128, 128, 128);
constexpr D3DCOLOR kColHeaderBg  = Argb(255,  40,  40,  56);
constexpr D3DCOLOR kColText      = Argb(255, 255, 255, 255);
constexpr D3DCOLOR kColTitle     = Argb(255, 255, 221, 102);
constexpr D3DCOLOR kColSubtle    = Argb(200, 200, 200, 200);
constexpr D3DCOLOR kColBtn       = Argb(255,  56,  64,  88);
constexpr D3DCOLOR kColHover     = Argb(255,  64,  96, 160);
constexpr D3DCOLOR kColRowAlt    = Argb( 40, 255, 255, 255);
constexpr D3DCOLOR kColRowSel    = Argb(110,  64,  96, 160);
constexpr D3DCOLOR kColError     = Argb(255, 255,  96,  96);
constexpr D3DCOLOR kColErrorHov  = Argb(255, 255, 140, 140);
constexpr D3DCOLOR kColSuccess   = Argb(255,  96, 255,  96);
constexpr D3DCOLOR kColSuccessHv = Argb(255, 140, 255, 140);
constexpr D3DCOLOR kColEditBg    = Argb(255,  20,  20,  28);
constexpr D3DCOLOR kColEditFocus = Argb(255,  40,  40,  64);

// ===========================================================================
// g_Guild+28 (0x1839984) — nom du MAÎTRE de guilde.
// ===========================================================================
// UI_GuildMgrWnd_OnClick 0x668B70 gate TOUTES les actions de maître par
// `Crt_Strcmp((unsigned int*)this + 7, byte_1673184)` — soit `*(g_Guild+28) vs
// g_SelfName` — aux EA 0x668DEF (inviter), 0x668EB9 (annonce), 0x6690EE (alliance),
// 0x66935E (quitter), 0x669426 (rang), 0x669583 (expulser), 0x669706 (dissoudre),
// 0x66984C (action 56). Refus = StrTable005 #467 (ou #466/#566 selon le chemin).
//
// Ce champ est explicitement « HORS PÉRIMÈTRE » de game::GuildRoster (cf. l'en-tête de
// Game/GuildSystem.h : « leader/co-leader @+28/+41/+54 »), et ce front ne possède pas
// Game/GuildSystem.h (règle #5) -> mirroir file-local en attendant.
//
// TODO [ancre 0x1839984 / Net_OnTeamFormationDispatch 0x491E70] : RIEN ne peuple ce
// mirroir aujourd'hui. Dans le binaire, +28 arrive avec le « blob » guilde de 1388 o
// copié par TeamFormationDispatch avant l'ouverture de la fenêtre (même blob qui
// remplit name[]/rank[]) — chemin non modélisé côté ClientSource. Tant qu'il est vide,
// IsSelfGuildMaster() renvoie false et les actions de maître REFUSENT (message #467) au
// lieu d'émettre : dégradation HONNÊTE et volontaire, cf. ci-dessous.
std::string g_guildMasterName; // g_Guild+28 (0x1839984)
} // namespace

// ---------------------------------------------------------------------------
// `*(g_Guild+28) == g_SelfName` — Crt_Strcmp(...) == 0 dans le binaire.
//
// ÉCART DE FIDÉLITÉ ASSUMÉ (documenté) : le binaire ne teste pas la vacuité. Deux
// chaînes vides y donnent Crt_Strcmp == 0, donc « je suis le maître » = VRAI. Ici les
// deux sources peuvent être vides (g_guildMasterName n'est jamais peuplé, cf. son TODO ;
// localPlayerName l'est par UI/LoginScene.cpp:1116 au choix du personnage), et un faux
// « c'est moi » ferait passer des actions de maître à un non-maître. On exige donc les
// deux non vides — MÊME politique de « dégradation honnête » que celle déjà écrite pour
// SelfState::localPlayerName (Game/GameState.h:340-347 : « toute comparaison
// == localPlayerName échoue proprement (jamais de faux "c'est moi") ») et que
// AllianceRoster::IsLeader (Game/GameState.h:465-467, `!name.empty() && ...`).
// ---------------------------------------------------------------------------
bool GuildWindow::IsSelfGuildMaster() const {
    const std::string& self = game::g_World.self.localPlayerName; // byte_1673184
    return !self.empty() && !g_guildMasterName.empty() && g_guildMasterName == self;
}

// `*(g_Guild+428)` = ligne sélectionnée (-1 = aucune) ; le nom vit à
// `this + 130*page + 13*row + 67` (UI_GuildMgrWnd_OnClick 0x668B70 @0x669180/0x6694B9/
// 0x669615). Ici l'indexation page/ligne est remplacée par le slot absolu 0..49
// (écart de disposition assumé, cf. UI/GuildWindow.h) — le NOM envoyé reste le même.
std::string GuildWindow::SelectedMemberName() const {
    if (selectedIdx_ < 0 || selectedIdx_ >= game::GuildRoster::kMaxMembers) return std::string();
    return game::g_Guild.members[static_cast<size_t>(selectedIdx_)].name;
}

void GuildWindow::SetFeedback(const std::string& text, D3DCOLOR color) {
    feedback_      = text;
    feedbackColor_ = color;
    feedbackUntil_ = lastGameTimeSec_ + kFeedbackDurationSec;
}

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
    mode_           = InputMode::None;
    scrollOffset_   = 0;
    selectedIdx_    = -1;   // `*(this+428) = -1` (UI_GuildMgrWnd_Open 0x667E20)
    pressedBtn_     = PressedBtn::None;
    pressedKickIdx_ = -1;
    pressedRowIdx_  = -1;
    feedback_.clear();
    feedbackUntil_  = -1.0f;
    nameEdit_.Clear();
    nameEdit_.SetFocused(false);

    // TODO [ancre UI_GuildCreate_Open 0x667DA0 @0x667e17] builder PRÉSENT mais gardes
    // non modélisées : le binaire demande le roster au serveur par Net_SendGuarded_2(1)
    // (Op75 sous-op 2) APRÈS deux gardes que ce front ne peut pas reproduire —
    //   (a) `Map_IsArenaZone() 0x54B690` -> refus StrTable005 #1352 : aucun helper
    //       équivalent n'existe côté ClientSource (l'état « zone arène » est PASSÉ en
    //       paramètre là où il sert, cf. Game/ComboPickupTick.cpp:152/155) ;
    //   (b) `Crt_Strcmp(dword_16746A8, "")` -> refus #462 : dword_16746A8 est un global
    //       DISTINCT de g_LocalGuildName 0x168740C (= game::g_World.allianceRoster
    //       .guildName) — NE PAS les confondre, son rôle exact reste disputé (cf.
    //       Game/NameplateLogic.h:43-50, Game/ChatCommands.cpp:104-105).
    // Émettre sans ces gardes serait moins fidèle que ne pas émettre : on n'émet pas.
    // À câbler quand (a) et (b) seront modélisés.
}

void GuildWindow::Close() {
    Dialog::Close();
    mode_ = InputMode::None;
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
// RE-CONFIRMÉ (W6, 2026-07-16) : UI_GuildMgrWnd_OnClick 0x668B70 recentre à l'identique
// à CHAQUE case de page (@0x668bc0/0x668bd9 page 1, 0x66996a page 2, 0x669a95 page 3,
// 0x669da0 page 4, 0x669f30 page 5) — `nWidth/2 - Sprite2D_GetWidth(...)/2`. Le
// centrage reste donc la seule partie prouvée bit-exacte de cette géométrie.
// La disposition (paging vs scroll, colonnes, bande de 7 boutons-icônes) demeure une
// réinvention assumée — cf. l'en-tête de UI/GuildWindow.h.
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

    if (mode_ == InputMode::None) {
        // 4 boutons d'action : 66 + 4 + 54 + 4 + 66 + 4 + 86 == 284 == actionRow.w.
        g.addBtn      = { g.actionRow.x,       g.actionRow.y, 66, kActionH };
        g.rankBtn     = { g.actionRow.x + 70,  g.actionRow.y, 54, kActionH };
        g.leaveBtn    = { g.actionRow.x + 128, g.actionRow.y, 66, kActionH };
        g.dissolveBtn = { g.actionRow.x + 198, g.actionRow.y, 86, kActionH };
        g.editBox = g.confirmBtn = g.cancelBtn = Rect{ 0, 0, 0, 0 };
    } else {
        g.editBox    = { g.actionRow.x, g.actionRow.y, 130, kActionH };
        g.confirmBtn = { g.editBox.x + g.editBox.w + 6, g.actionRow.y, 64, kActionH };
        g.cancelBtn  = { g.confirmBtn.x + g.confirmBtn.w + 6, g.actionRow.y, 56, kActionH };
        g.addBtn = g.rankBtn = g.leaveBtn = g.dissolveBtn = Rect{ 0, 0, 0, 0 };
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
// Actions réseau
// ============================================================================
// TOUTES les émissions ci-dessous lisent net::GlobalNetClient() (Net/NetClient.h:67-68)
// = restitution du singleton g_NetClient 0x8156A0 que les builders du binaire adressent
// en global sans le recevoir en paramètre. Le pointeur est renseigné par
// ConnectLoginServer/ConnectGameServer (Net/Login.cpp:131/313) : il est NON NUL dès
// qu'une session est amorcée — contrairement à l'ancien membre net_ (jamais Bind()é,
// donc toujours nul = code mort), supprimé par cette vague.
//
// ÉCART DE FIDÉLITÉ COMMUN À KICK/QUITTER/DISSOUDRE (documenté, pas caché) : le binaire
// n'émet PAS au clic. Il ouvre une MsgBox de confirmation (UI_MsgBox_Open 0x5C08C0 sur
// dword_1822438) et c'est son relâchement (UI_MsgBox_OnLButtonUp 0x5C1170, jumptable
// 005C0BE5) qui émet — case 16 -> Guarded_6 (@0x5c1181), case 17 -> Guarded_8
// (@0x5c119a), case 18 -> Guarded_4 (@0x5c11ae). Cette fenêtre n'est pas branchée sur
// ce registre de MsgBox (game::g_Client.prompt modélise l'état, pas le routage des 60+
// cases du jumptable) : on émet directement au clic, APRÈS avoir reproduit les mêmes
// gardes. Le PAQUET SUR LE FIL est identique ; seule l'étape de confirmation manque.

// « X » d'une ligne -> expulsion. Ancre : UI_GuildMgrWnd_OnClick 0x668B70, branche
// `*(this+409)` @0x669527 (bouton unk_904878, x+213 y+69) -> MsgBox 17 (StrTable005
// #473) -> UI_MsgBox_OnLButtonUp @0x5c1190-0x5c119a : Net_SendGuarded_8(byte_183A0F4).
void GuildWindow::DoKick() {
    // Garde 1 @0x669583 : doit être le maître, sinon LABEL_70 -> StrTable005 #467.
    if (!IsSelfGuildMaster()) { game::g_Client.msg.System(game::Str(467)); return; }
    // Garde 2 @0x6695C4 : une ligne doit être sélectionnée (`*(this+428) != -1`), sinon
    // LABEL_93 -> #472. Dans le binaire « Expulser » est un bouton de PAGE (unk_904878,
    // x+213 y+69) agissant sur la ligne sélectionnée, d'où ce contrôle. Ici le « X »
    // est PAR LIGNE (invention de disposition, cf. en-tête du .h) : la ligne visée est
    // implicite, donc cette garde est structurellement satisfaite. Conservée quand même
    // — elle reste le contrôle du binaire, et couvre un latch incohérent.
    if (pressedKickIdx_ < 0) { game::g_Client.msg.System(game::Str(472)); return; }

    const std::string target =
        game::g_Guild.members[static_cast<size_t>(pressedKickIdx_)].name;
    // Garde 3 @0x669615/0x669626 : Crt_Strcmp(membre, g_SelfName) == 0 (la cible est
    // soi-même) -> refus #474 ; sinon -> MsgBox 17 puis envoi.
    if (target.empty() || target == game::g_World.self.localPlayerName) {
        game::g_Client.msg.System(game::Str(474));
        return;
    }

    net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
    if (!nc) return;                              // pas de session : rien à émettre

    // Net_SendGuarded_8 0x593420 (Net/SendPackets.h:185) : Op75 sous-op 8, payload 13 o.
    // TODO [ancre 0x183A0F4] : le binaire envoie byte_183A0F4 (= g_Guild+1932), PAS
    // directement `members[ligne]`. Les 9 xrefs de byte_183A0F4 sont TOUTES en LECTURE
    // (Net_OnTeamFormationDispatch 0x491E70 @0x49286A/0x492892/0x492D4D/0x492D5F/
    // 0x492D87 ; UI_MsgBox_OnLButtonUp @0x5C1190/0x5C1224/0x5C123F/0x5C1D99) — AUCUN
    // site d'écriture localisé en statique (UI_GuildMgrWnd_OnMouseDown 0x667F10 ne pose
    // que `*(this+428)=i`, l'index, sans copier le nom ; écriture probable via adresse
    // calculée, à relever en dynamique x32dbg). Envoyer le nom de la ligne sélectionnée
    // est une INFERENCE — étayée par la garde 3 ci-dessus, qui valide justement le nom
    // de la LIGNE SÉLECTIONNÉE juste avant d'ouvrir la MsgBox 17 (elle serait sans objet
    // si +1932 portait autre chose) — mais elle reste NON PROUVÉE. À confirmer.
    char name13[13] = {};
    const size_t n = std::min(target.size(), sizeof(name13) - 1);
    std::memcpy(name13, target.data(), n);
    net::Net_SendGuarded_8(*nc, name13);

    // AUCUNE mutation locale : le binaire ne retire le membre qu'à la RÉPONSE serveur
    // (Net_OnTeamFormationDispatch 0x491E70 case 8 @0x492874-0x492923 -> RemoveMember).
    // L'ancien `game::g_Guild.RemoveMember(...)` posé ici en Passe 3 était une mise à
    // jour optimiste que le binaire ne fait PAS — supprimé.
    SetFeedback("Expulsion demandee : " + target, kColError);
}

// « Quitter » -> UI_GuildMgrWnd_OnClick 0x668B70, branche `*(this+407)` @0x669305
// (bouton unk_904D18, x+101 y+69) -> MsgBox 18 (StrTable005 #477) ->
// UI_MsgBox_OnLButtonUp @0x5c11a9-0x5c11ae : Net_SendGuarded_4().
void GuildWindow::DoLeave() {
    // Garde @0x66935E — INVERSÉE par rapport à Dissoudre : Crt_Strcmp(master, self) == 0
    // (je SUIS le maître) -> refus StrTable005 #466 ; un maître ne « quitte » pas, il
    // dissout. Le refus n'est possible que quand IsSelfGuildMaster() est fiable : tant
    // que le mirroir g_guildMasterName est vide (cf. son TODO), il renvoie false et ce
    // chemin ÉMET — c'est le comportement d'un non-maître, donc le cas majoritaire.
    if (IsSelfGuildMaster()) { game::g_Client.msg.System(game::Str(466)); return; }

    net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
    if (!nc) return;

    // Net_SendGuarded_4 0x593210 (Net/SendPackets.h:68) : Op75 sous-op 4, sans payload.
    net::Net_SendGuarded_4(*nc);
    SetFeedback("Depart de la guilde demande.", kColError);
}

// « Dissoudre » -> UI_GuildMgrWnd_OnClick 0x668B70, branche `*(this+410)` @0x6696AA
// (bouton unk_904E40, x+269 y+69) -> MsgBox 16 (StrTable005 #469) ->
// UI_MsgBox_OnLButtonUp @0x5c117c-0x5c1181 : Net_SendGuarded_6().
void GuildWindow::DoDissolve() {
    // Garde @0x669706 : Crt_Strcmp(master, self) != 0 (je ne suis PAS le maître)
    // -> refus StrTable005 #467.
    if (!IsSelfGuildMaster()) { game::g_Client.msg.System(game::Str(467)); return; }

    net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
    if (!nc) return;

    // Net_SendGuarded_6 0x593320 (Net/SendPackets.h:127) : Op75 sous-op 6, sans payload.
    net::Net_SendGuarded_6(*nc);
    SetFeedback("Dissolution demandee.", kColError);
}

// ============================================================================
// Validation de la saisie (mode_ == AddMember | SetRank)
// ============================================================================
void GuildWindow::Confirm() {
    if (mode_ == InputMode::AddMember) {
        // Ancre : Guild_AddMemberFromInput 0x66BCD0 (seul appelant d'origine de
        // Net_SendOp76, xref unique @0x66bd5b), déclenché par UI_GuildMgrWnd_OnClick
        // 0x668B70 page 2, branche `*(this+411)` @0x6699fc (bouton unk_906E0C,
        // x+272 y+71).
        const std::string name = nameEdit_.Text();

        // @0x66bcfc-0x66bd04 : GetWindowTextA == 0 (saisie vide) -> retour immédiat,
        // SANS message et SANS envoi. Reste en mode saisie.
        if (name.empty()) return;

        // @0x66bd14 : SetWindowTextA(dword_1668FE0, "") — la boîte est vidée AVANT le
        // test du dictionnaire.
        nameEdit_.Clear();

        // @0x66bd26 : maybe_Dict001_MatchWord(g_BannedWordList, name) -> mot banni ->
        // StrTable005 #112 et AUCUN envoi. Le dictionnaire (0x4C1410) est hors périmètre
        // de Game/GuildSystem.h -> `banned` toujours faux ici (cf. GuildRoster::AddMember,
        // « banned est calculé par l'appelant »), donc ce refus ne se déclenche jamais
        // encore. GuildRoster::AddMember est un PRÉDICAT const (il ne mute PAS members[]) :
        // il rend false ssi le nom est vide ou banni — la vacuité étant déjà traitée
        // ci-dessus, un false ici signifie « banni ».
        const bool banned = false;
        if (!game::g_Guild.AddMember(name, banned)) {
            game::g_Client.msg.System(game::Str(112));
            return;
        }

        net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
        if (!nc) return;

        // @0x66bd5b : Net_SendOp76(&g_AutoPlayMgr, String) — opcode 0x4C, payload brut
        // 61 o (Net/SendPackets.h:108). Le nom lu dans l'edit-box est copié tel quel
        // (NUL-rempli), sans structure additionnelle.
        char name61[61] = {};
        const size_t n = std::min(name.size(), sizeof(name61) - 1);
        std::memcpy(name61, name.data(), n);
        net::Net_SendOp76(*nc, name61);

        // Le roster n'est mis à jour qu'à la réponse serveur -> aucune mutation locale.
        // Guild_AddMemberFromInput ne touche PAS `*(this+426)` : on RESTE sur la page
        // « inviter » après l'envoi (seul le bouton Annuler `*(this+412)` @0x669a5d
        // repasse à la page 1). Fidèle.
        SetFeedback("Demande d'ajout envoyee : " + name, kColSuccess);
        return;
    }

    if (mode_ == InputMode::SetRank) {
        // Ancre : UI_GuildMgrWnd_OnClick 0x668B70 page 4, branche `*(this+415)` @0x669DE4
        // (bouton unk_902C24, x+71 y+39).
        //   @0x669e43 : GetWindowTextA(dword_1668FF4, this+1945, 5)
        //   @0x669e78 : Net_SendGuarded_10(this + 130*page + 13*row + 67, this+1945)
        //   @0x669e80 : *(this+426) = 1  (retour page roster)
        // AUCUNE garde à cet endroit : les contrôles (maître / ligne / cible != soi) sont
        // faits à l'ENTRÉE de la page 4 (@0x6693CB, cf. OnClick ci-dessous). Le binaire
        // n'exige PAS un rang non vide — il envoie les 5 octets tels quels. Reproduit.
        const std::string target = SelectedMemberName();
        net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
        if (nc && !target.empty()) {
            // Net_SendGuarded_10 0x593550 (Net/SendPackets.h:243) : Op75 sous-op 10,
            // payload 13 o (nom) + 5 o (rang) CONTIGUS — les deux Crt_Memcpy (13 puis 5)
            // ont lieu AVANT la garde anti-rejeu, cf. décompilation 0x593580/0x593595.
            // Le rang est une CHAÎNE de 5 o (buffer this+1945), PAS un entier.
            char name13[13] = {};
            const size_t n = std::min(target.size(), sizeof(name13) - 1);
            std::memcpy(name13, target.data(), n);

            char rank5[5] = {};
            const std::string rankText = nameEdit_.Text();
            const size_t r = std::min(rankText.size(), sizeof(rank5) - 1);
            std::memcpy(rank5, rankText.data(), r);

            net::Net_SendGuarded_10(*nc, name13, rank5);
            SetFeedback("Rang envoye pour " + target + " : " + rankText, kColSuccess);
        }
        // @0x669e80 : retour page roster, que l'envoi ait eu lieu ou non.
        CancelInput();
        return;
    }
}

void GuildWindow::CancelInput() {
    // Boutons Annuler d'origine : page 2 `*(this+412)` @0x669a5d, page 4 `*(this+416)`
    // @0x669ef8 — tous deux se contentent de `*(this+426) = 1` (retour page roster) et
    // de relâcher le focus de l'edit-box (UI_FocusEditBox(&g_UIEditBoxMgr, 0)).
    mode_ = InputMode::None;
    nameEdit_.Clear();
    nameEdit_.SetFocused(false);
    nameEdit_.SetMaxLength(static_cast<size_t>(game::GuildRoster::kNameStride - 1));
}

// ============================================================================
// Événements souris (latch armé au down, validé au relâchement — pattern MsgBoxDialog)
// ============================================================================
bool GuildWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    const Geom g = ComputeGeometry(lastScreenW_, lastScreenH_);
    pressedBtn_     = PressedBtn::None;
    pressedKickIdx_ = -1;
    pressedRowIdx_  = -1;

    if (PointInRect(x, y, g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h)) {
        pressedBtn_ = PressedBtn::Close;
    } else if (mode_ != InputMode::None) {
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
    } else if (PointInRect(x, y, g.rankBtn.x, g.rankBtn.y, g.rankBtn.w, g.rankBtn.h)) {
        pressedBtn_ = PressedBtn::Rank;
    } else if (PointInRect(x, y, g.leaveBtn.x, g.leaveBtn.y, g.leaveBtn.w, g.leaveBtn.h)) {
        pressedBtn_ = PressedBtn::Leave;
    } else if (PointInRect(x, y, g.dissolveBtn.x, g.dissolveBtn.y, g.dissolveBtn.w, g.dissolveBtn.h)) {
        pressedBtn_ = PressedBtn::Dissolve;
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
            // Sélection de ligne = `*(this+428) = i` (UI_GuildMgrWnd_OnMouseDown
            // 0x667F10) ; le sélecteur d'origine est surligné à (x+17, y+20*i+190).
            const Rect r = RowRect(g, i);
            if (PointInRect(x, y, r.x, r.y, r.w, r.h)) {
                pressedBtn_    = PressedBtn::Row;
                pressedRowIdx_ = visible[static_cast<size_t>(scrollOffset_ + i)];
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
    const PressedBtn armed = pressedBtn_;

    // Relâché avant tout traitement : Confirm()/DoKick() peuvent changer de mode et donc
    // de géométrie, et aucun chemin ci-dessous ne doit garder un latch périmé.
    pressedBtn_ = PressedBtn::None;

    switch (armed) {
        case PressedBtn::Close:
            if (PointInRect(x, y, g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h))
                Close();
            break;

        case PressedBtn::Add:
            if (PointInRect(x, y, g.addBtn.x, g.addBtn.y, g.addBtn.w, g.addBtn.h)) {
                // Entrée page 2 — UI_GuildMgrWnd_OnClick 0x668B70, branche `*(this+404)`
                // @0x668D94 (bouton unk_9043D8, x+157 y+49). Garde @0x668DEF : seul le
                // MAÎTRE peut ouvrir la page « inviter », sinon StrTable005 #467. Cette
                // garde MANQUAIT à la Passe 3 (le bouton était ouvert à tous).
                if (!IsSelfGuildMaster()) {
                    game::g_Client.msg.System(game::Str(467));
                    break;
                }
                // @0x668E29 : *(this+426) = 2, focus edit-box 8, SetWindowTextA(…, "").
                mode_ = InputMode::AddMember;
                nameEdit_.SetMaxLength(static_cast<size_t>(game::GuildRoster::kNameStride - 1));
                nameEdit_.Clear();
                nameEdit_.SetFocused(true);
                feedback_.clear();
                feedbackUntil_ = -1.0f;
            }
            break;

        case PressedBtn::Rank:
            if (PointInRect(x, y, g.rankBtn.x, g.rankBtn.y, g.rankBtn.w, g.rankBtn.h)) {
                // Entrée page 4 — UI_GuildMgrWnd_OnClick 0x668B70, branche `*(this+408)`
                // @0x6693CB (bouton unk_904750, x+157 y+69).
                // Garde 1 @0x669426 : maître, sinon LABEL_70 -> #467.
                if (!IsSelfGuildMaster()) {
                    game::g_Client.msg.System(game::Str(467));
                    break;
                }
                // Garde 2 @0x669467 : ligne sélectionnée, sinon LABEL_93 -> #472.
                if (selectedIdx_ < 0) {
                    game::g_Client.msg.System(game::Str(472));
                    break;
                }
                // Garde 3 @0x6694B9/0x6694CB : Crt_Strcmp(membre, g_SelfName) == 0 (la
                // cible est soi-même) -> refus #556 (et NON #546, qui est le refus
                // symétrique de la page 5 « alliance » @0x669191).
                const std::string target = SelectedMemberName();
                if (target.empty() || target == game::g_World.self.localPlayerName) {
                    game::g_Client.msg.System(game::Str(556));
                    break;
                }
                // @0x6694F3 : *(this+426) = 4, focus edit-box 13, SetWindowTextA(…, "").
                mode_ = InputMode::SetRank;
                nameEdit_.SetMaxLength(static_cast<size_t>(kRankMaxChars));
                nameEdit_.Clear();
                nameEdit_.SetFocused(true);
                feedback_.clear();
                feedbackUntil_ = -1.0f;
            }
            break;

        case PressedBtn::Leave:
            if (PointInRect(x, y, g.leaveBtn.x, g.leaveBtn.y, g.leaveBtn.w, g.leaveBtn.h))
                DoLeave();
            break;

        case PressedBtn::Dissolve:
            if (PointInRect(x, y, g.dissolveBtn.x, g.dissolveBtn.y, g.dissolveBtn.w, g.dissolveBtn.h))
                DoDissolve();
            break;

        case PressedBtn::ScrollUp:
            if (PointInRect(x, y, g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h)) {
                scrollOffset_ = std::max(0, scrollOffset_ - 1);
                // `*(this+428) = -1` accompagne tout changement de page d'origine
                // (@0x668CE6 page précédente, @0x668D7D page suivante) : le défilement
                // est l'analogue local du paging -> on désélectionne aussi.
                selectedIdx_ = -1;
            }
            break;

        case PressedBtn::ScrollDown:
            if (PointInRect(x, y, g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h)) {
                scrollOffset_ = std::min(MaxScroll(), scrollOffset_ + 1);
                selectedIdx_  = -1; // idem @0x668D7D
            }
            break;

        case PressedBtn::Confirm:
            if (PointInRect(x, y, g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h))
                Confirm();
            break;

        case PressedBtn::Cancel:
            if (PointInRect(x, y, g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h))
                CancelInput();
            break;

        case PressedBtn::Row: {
            const std::vector<int> visible = VisibleMemberIndices();
            const int rows = std::min(kVisibleRows,
                                       static_cast<int>(visible.size()) - scrollOffset_);
            for (int i = 0; i < rows; ++i) {
                if (visible[static_cast<size_t>(scrollOffset_ + i)] != pressedRowIdx_) continue;
                const Rect r = RowRect(g, i);
                if (PointInRect(x, y, r.x, r.y, r.w, r.h))
                    selectedIdx_ = pressedRowIdx_; // `*(this+428) = i` (0x667F10)
                break;
            }
            break;
        }

        case PressedBtn::Kick: {
            const std::vector<int> visible = VisibleMemberIndices();
            const int rows = std::min(kVisibleRows,
                                       static_cast<int>(visible.size()) - scrollOffset_);
            for (int i = 0; i < rows; ++i) {
                if (visible[static_cast<size_t>(scrollOffset_ + i)] != pressedKickIdx_) continue;
                const Rect kr = KickRect(g, i);
                if (PointInRect(x, y, kr.x, kr.y, kr.w, kr.h))
                    DoKick();
                break;
            }
            break;
        }

        default: break;
    }

    pressedKickIdx_ = -1;
    pressedRowIdx_  = -1;
    return consumed;
}

// ============================================================================
// Clavier — voir la limitation documentée en tête de UI/GuildWindow.h (pas de WM_CHAR
// routé par l'UIManager, seulement OnKey(vk) = WM_KEYDOWN).
// ============================================================================
bool GuildWindow::OnKey(int vk) {
    if (!bOpen_) return false;

    if (mode_ != InputMode::None && nameEdit_.Focused()) {
        if (vk == VK_RETURN) { Confirm();     return true; }
        if (vk == VK_ESCAPE) { CancelInput(); return true; }
        if (vk == VK_BACK || vk == VK_DELETE || vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_HOME || vk == VK_END) {
            nameEdit_.OnKey(vk);
            return true;
        }
        // VK_0..VK_9 (0x30-0x39) et VK_A..VK_Z (0x41-0x5A) coïncident avec leurs codes
        // ASCII en Win32 -> saisie basique majuscules/chiffres/espace uniquement.
        // En mode SetRank le binaire lit une chaîne de 5 o via GetWindowTextA sans
        // filtrer les caractères : on n'ajoute donc PAS de restriction aux chiffres.
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
            const int memberIdx = visible[static_cast<size_t>(scrollOffset_ + i)];
            const bool hovered = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
            if (memberIdx == selectedIdx_) ctx.FillRect(r.x, r.y, r.w, r.h, kColRowSel);
            else if (hovered) ctx.FillRect(r.x, r.y, r.w, r.h, kColHover);
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

        if (mode_ != InputMode::None) {
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
            const Rect* btns[4]  = { &g.addBtn, &g.rankBtn, &g.leaveBtn, &g.dissolveBtn };
            for (int i = 0; i < 4; ++i) {
                const Rect& b = *btns[i];
                const bool hov = PointInRect(cursorX, cursorY, b.x, b.y, b.w, b.h);
                ctx.FillRect(b.x, b.y, b.w, b.h, hov ? kColHover : kColBtn);
                ctx.DrawFrame(b.x, b.y, b.w, b.h, kColBorder, 1);
            }
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

    if (mode_ != InputMode::None) {
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
        ctx.Text("Ajouter",   g.addBtn.x + 6,      g.addBtn.y + 6,      kColText);
        ctx.Text("Rang",      g.rankBtn.x + 8,     g.rankBtn.y + 6,     kColText);
        ctx.Text("Quitter",   g.leaveBtn.x + 6,    g.leaveBtn.y + 6,    kColText);
        ctx.Text("Dissoudre", g.dissolveBtn.x + 8, g.dissolveBtn.y + 6, kColText);
    }

    if (!feedback_.empty())
        ctx.Text(feedback_.c_str(), g.feedbackArea.x, g.feedbackArea.y, feedbackColor_);
}

// ===========================================================================
// ÉMISSIONS GUILDE NON PORTÉES — builders TOUS PRÉSENTS dans Net/SendPackets.h ; ce
// qui manque est l'ÉTAT ou l'UI, jamais le paquet. Aucun `missingBuilder` sur ce front.
// ===========================================================================
// 1) ANNONCE — Net_SendGuarded_5 0x593290 (Net/SendPackets.h:98), Op75 sous-op 5,
//    payload 204 o = 4 champs de 51 o CONTIGUS (g_Guild+1724/1775/1826/1877).
//    Ancre : UI_GuildMgrWnd_OnClick 0x668B70 page 3, `*(this+413)` @0x669AD9 ->
//    4x GetWindowTextA(dword_1668FE4/E8/EC/F0, this+1724+51*i, 51) @0x669b3e-0x669b87,
//    puis 8 gardes (4x maybe_Dict001_MatchWord + 4x Str_ContainsForbiddenToken 0x556370)
//    -> refus #112, sinon Net_SendGuarded_5(this+1724) @0x669ca9.
//    NON PORTÉ : exige 4 champs de saisie libres de 50 caractères. L'UIManager ne route
//    pas WM_CHAR (cf. en-tête du .h) -> saisie limitée aux majuscules/chiffres/espace,
//    inutilisable pour une annonce. Str_ContainsForbiddenToken 0x556370 n'est pas non
//    plus modélisé côté ClientSource.
//    NB : les champs LUS (+1724..+1927) sont DISTINCTS des champs affichés à l'entrée de
//    la page (+1170/+1221/+1272/+1323, @0x668f60-0x66907f) — deux buffers, ne pas fusionner.
//
// 2) ALLIANCE / GUERRE (page 5) — Net_SendGuarded_14 0x593770 (Net/SendPackets.h:128),
//    Op75 sous-op 14, arg `char` émis sur 4 OCTETS LE (Crt_Memcpy(v2, &a1, 4) @0x5937a0).
//    Ancres : entrée de page `*(this+417)` @0x66977A -> Net_SendGuarded_2(3) @0x6697c3 +
//    `*(this+426)=5` + `*(this+18739) = dword_1687450` @0x6697dd ; sélecteur 5 choix
//    `*(this+420+i)` @0x66A08C -> `*(this+18739) = i` @0x66a11e ; validation
//    `*(this+418)` @0x669F74 -> garde `dword_16746B8 <= 1` (sinon #1718) ->
//    Net_SendGuarded_14(*(this+18739)) @0x669fe7.
//    NON PORTÉ : `*(g_Guild+74956)` (choix) et `dword_1687450` / `dword_16746B8` ne sont
//    pas modélisés (dword_16746B8 sert aussi de garde à UI_Guild_InviteRequest 0x5ED540
//    @0x5ed5c3, rôle exact non établi).
//
// 3) ALLIANCE A/B — Net_SendGuarded_9 0x5934B0 (Net/SendPackets.h:214), Op75 sous-op 9,
//    payload 13 o (nom) + i32 @13. Ancre : `*(this+406)` @0x669092 -> gardes maître
//    (#467) / ligne (#472) / cible != soi (#546) / si `*(this + 10*page + row + 180) == 2`
//    ET `+41 != "" ET +54 != ""` -> refus #547 ; puis MsgBox 24 (`*(this+482)=1`) ou 25
//    (`*(this+482)=2`) -> UI_MsgBox_OnLButtonUp @0x5c1222-0x5c122e / @0x5c123d-0x5c1249 :
//    Net_SendGuarded_9(byte_183A0F4, 1 ou 2) — la VALEUR vient du numéro de case MsgBox,
//    pas de `*(this+482)`.
//    NON PORTÉ : le statut par membre `*(this + 10*page + row + 180)` et les champs
//    co-leader +41/+54 sont « HORS PÉRIMÈTRE » de game::GuildRoster (cf. GuildSystem.h).
//
// 4) ACTION 56 — Net_SendGuarded_17 0x593800 (Net/SendPackets.h:157), Op75 sous-op 17,
//    payload 13 o + 13 o + i32 @26 (3e arg `char` -> Crt_Memcpy(v6, &a3, 4) @0x59385a).
//    Ancre : `*(this+425)` @0x6697F7 -> gardes maître (#566, PAS #467) / ligne (#472) /
//    `g_Guild+1932 != g_SelfName` (sinon #2133) -> MsgBox 56 (#2107) ->
//    UI_MsgBox_OnLButtonUp @0x5c1d8d-0x5c1da3 : Net_SendGuarded_17(byte_183A0F4,
//    byte_1673184, dword_1839ED8) — dword_1839ED8 = g_Guild+1392, poussé par VALEUR.
//    NON PORTÉ : g_Guild+1392 n'est pas modélisé et le rôle de cette action reste
//    indéterminé (aucune chaîne exploitable) — nommer/émettre serait deviner.
//
// 5) CRÉATION DE GUILDE (Net_SendMenu_1 0x5938C0, Op79 sous-op 1) et DEMANDE
//    D'INVITATION (Net_SendGuarded_2(2)) — HORS DE CE FRONT : leurs déclencheurs sont
//    des méthodes de la fenêtre PNJ (UI_Guild_CreateRequest 0x5ED460 et
//    UI_Guild_InviteRequest 0x5ED540 appellent UI_NpcWin_CloseRestore 0x5DC1F0 sur
//    `this`), pas de la fenêtre Guilde. Elles ouvrent MsgBox 27 / 26 et l'envoi a lieu
//    dans UI_MsgBox_OnLButtonUp @0x5c1273 / @0x5c125f. À câbler depuis la fenêtre PNJ.
//    ⚠️ CORRECTION du rapport EXTRACT amont, qui donnait la garde de 0x5ED460 comme
//    « g_SelfLevel >= 40 » : la décompilation dit l'INVERSE — `if (g_SelfLevel <= 39)`
//    @0x5ed4a7 OUVRE la confirmation (MsgBox 27, #600), et le niveau >= 40 donne le
//    message de REFUS #599 @0x5ed4ba. (Cohérent avec le nom IDA « guild/mentor create » :
//    un personnage de bas niveau demande un mentor.) Ne pas reprendre l'inversion.

} // namespace ts2::ui

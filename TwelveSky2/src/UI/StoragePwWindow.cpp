// UI/StoragePwWindow.cpp — implémentation de UI_StoragePwWnd (0x666900 / dword_1839950)
// = fenêtre QUITTER / DISSOUDRE L'ALLIANCE (le symbole IDA est trompeur, cf. le .h).
//
// Aucun include Net/ : cette fenêtre n'émet RIEN — c'est PROUVÉ, pas une omission
// (cf. bandeau « FAIT DÉCISIF » de UI/StoragePwWindow.h). Pas de contrainte
// d'ordre <winsock2.h>/<windows.h> ici (même profil d'includes que UI/SocialWindow.cpp).
#include "UI/StoragePwWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameState.h"      // game::g_World.allianceRoster / .self.localPlayerName
#include "Scene/SceneManager.h"  // ts2::g_SceneSubState (miroir de g_SceneSubState 0x1676184)

#include <cstddef>

namespace ts2::ui {

namespace {

// Instance active (une seule dans tout le process, possédée par GameWindows).
StoragePwWindow* g_activeStoragePw = nullptr;

// Fond de panneau réel « best effort » — repli automatique sur kColPanelBg si absent
// (cf. méthodologie dans UI/PanelSkin.h). Le vrai fond est le sprite unk_8F70D4
// (EA 0x667082), dont le fichier .IMG n'est pas résolu statiquement.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00301.IMG");

constexpr uint32_t kSysMsgColorAddr = 0x84DFD8; // g_SysMsgColor
constexpr uint32_t kSelfMorphNpcId  = 0x1675A98; // g_SelfMorphNpcId

// g_SysMsgColor 0x84DFD8 — longue traîne via Var() (convention UI/PartyWindow.cpp:47).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
}

// Map_IsArenaZone 0x54B690 : `return g_SelfMorphNpcId >= 270 && g_SelfMorphNpcId <= 274;`
// (le global 0x1675A98 est g_SelfMorphNpcId malgré le libellé « map id » du commentaire
// IDB). Porté file-local, EXACTEMENT comme UI/PartyWindow.cpp:40-43 — ce front ne
// possède pas Game/MapWarp.h, où cet helper devrait être mutualisé à terme.
bool Map_IsArenaZone() {
    const int32_t morphNpcId = game::g_Client.VarGet(kSelfMorphNpcId);
    return morphNpcId >= 270 && morphNpcId <= 274;   // EA 0x54B6BE
}

// Miroir de Crt_Stricmp 0x76668B (comparaison ASCII insensible à la casse) —
// même convention file-local que Game/QuestSystem.cpp:25 (StriEqualBounded).
// N'est utilisé QUE par le bouton « dissoudre » : voir l'asymétrie Strcmp/Stricmp
// documentée dans HandleDisbandClick().
bool StriEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 'a' + 'A');
        if (ca != cb) return false;
    }
    return true;
}

// g_AllianceRosterNames[0] 0x16749B8 = slot 0 du mini-roster = LEADER.
// Modélisé par game::g_World.allianceRoster.memberNames[0] (Game/GameState.h:534).
const std::string& RosterLeader() {
    return game::g_World.allianceRoster.memberNames[0];
}

// byte_1673184 = nom du joueur local -> game::g_World.self.localPlayerName
// (Game/GameState.h:427). Peuplé par UI/LoginScene.cpp au choix du personnage
// (cf. UI/GuildWindow.cpp:71). Même usage que UI/GuildWindow.cpp:79.
const std::string& SelfName() {
    return game::g_World.self.localPlayerName;
}

} // namespace

// ============================================================================
// Cycle de vie / instance active
// ============================================================================
StoragePwWindow::StoragePwWindow() {
    RecomputeCenter(ts2::kRefWidth, ts2::kRefHeight);
    g_activeStoragePw = this;
}

StoragePwWindow::~StoragePwWindow() {
    if (g_activeStoragePw == this) g_activeStoragePw = nullptr;
}

StoragePwWindow* StoragePwWindow::Active() { return g_activeStoragePw; }

// UI_StoragePwWnd_Open 0x666960.
void StoragePwWindow::Open() {
    // (a) Garde arène : refus TOTAL, message système, AUCUNE ouverture (EA 0x66696E).
    if (Map_IsArenaZone()) {
        game::g_Client.msg.System(game::Str(kStrArenaRefused), SysMsgColor()); // EA 0x666987/0x666992
        return;
    }

    // (b) `Crt_Strcmp(g_AllianceRosterNames, &String)` (EA 0x6669A3) : `String` 0x7EC95F
    // est la CHAÎNE VIDE (prouvé par read_cstring, length 0) -> ce test signifie
    // « le slot 0 du roster est-il vide ? ». Si oui (strcmp == 0) : message #355,
    // pas d'ouverture (EA 0x6669B5..0x6669CB).
    if (RosterLeader().empty()) {
        game::g_Client.msg.System(game::Str(kStrNoAlliance), SysMsgColor());   // EA 0x6669C0/0x6669CB
        return;
    }

    // *(this+2) = 1 (EA 0x6669D5) + boucle `for (i=0;i<3;++i) *(this+i+3)=0` (EA 0x6669DC).
    Dialog::Open();
    leaveLatch_   = false;
    disbandLatch_ = false;
    closeLatch_   = false;
}

// UI_StoragePwWnd_Close 0x666A10 : `if (*(this+2)) *(this+2) = 0;` — rien d'autre.
void StoragePwWindow::Close() {
    Dialog::Close();
}

// ============================================================================
// UI_StoragePwWnd_ProcNet 0x666EB0 — toggle par commande clavier 25 (DIK_P)
// ============================================================================
// Transcription intégrale, y compris l'ASYMÉTRIE prouvée : le chemin de FERMETURE
// (fenêtre déjà ouverte, EA 0x666EBC) n'est PAS gardé par la scène — seul le
// chemin d'OUVERTURE l'est (`g_SceneMgr == 6 && g_SceneSubState == 4`, EA 0x666ED6).
//
// Le binaire balaie la file de commandes pour trouver le PREMIER enregistrement
// « appuyé » (`g_UiCmdQueueFlags[5*i] & 0x80`) puis teste `== 25`. Côté C++ ce
// balayage est DÉJÀ porté (App/PlayerInputController.cpp:138
// `in.FirstKeyDownDik()`, transcription de la même boucle 0x50C726) : `cmdId` est
// donc exactement le résultat du balayage, et le test `cmdId == 25` est l'équivalent
// fidèle (et non un test « 25 est-il quelque part dans la file »).
//
// g_SceneMgr == 6 : aucun miroir global côté C++ (la scène courante vit dans
// SceneManager, privée). Ce n'est PAS une lacune ici — le seul appelant prévu,
// PlayerInputController::RouteSceneKey, n'est atteignable QU'APRÈS la garde
// d'entrée `scene != Scene::InGame || g_SceneSubState != 4` de
// PlayerInputController::Update (App/PlayerInputController.cpp:30, transcription de
// 0x50B7EC) : `g_SceneMgr == 6` est donc DÉJÀ garanti au site d'appel. On vérifie
// tout de même le sous-état, qui, lui, est disponible en global (ts2::g_SceneSubState).
bool StoragePwWindow::ProcKeyCommand(int cmdId) {
    StoragePwWindow* w = g_activeStoragePw;
    if (!w) return false;

    if (w->IsOpen()) {                       // EA 0x666EBC
        if (cmdId == kCmdToggle) {           // EA 0x666FB5
            w->Close();                      // EA 0x666FBC
            return true;                     // EA 0x666FC1
        }
        return false;                        // EA 0x666FC8
    }

    if (g_SceneSubState != 4) return false;  // moitié « g_SceneSubState == 4 » de l'EA 0x666ED6

    if (cmdId == kCmdToggle) {               // EA 0x666F39
        // UI_CloseAllDialogs(dword_1821D4C, 1) (EA 0x666F44) : ouvrir cette fenêtre
        // ferme toutes les autres — UIManager::CloseAll() est le miroir modélisé
        // (UI_CloseAllDialogs 0x5AC590, cf. UI/UIManager.h:217).
        UIManager::Instance().CloseAll();
        w->Open();                           // EA 0x666F4C — peut REFUSER (arène / pas d'alliance)
        return true;                         // EA 0x666F51 — le binaire renvoie 1 même en cas de refus
    }
    return false;                            // EA 0x666F58
}

// ============================================================================
// Géométrie
// ============================================================================
void StoragePwWindow::RecomputeCenter(int screenW, int screenH) {
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

StoragePwWindow::Rect StoragePwWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

StoragePwWindow::Rect StoragePwWindow::LeaveButtonRect() const {
    return { x_ + kBtnX, y_ + kBtnLeaveY, kBtnW, kBtnH };
}

// Bande DESSINÉE / testée au clic-enfoncé : +110 (EA 0x66711E, 0x666B00).
StoragePwWindow::Rect StoragePwWindow::DisbandButtonRect() const {
    return { x_ + kBtnX, y_ + kBtnDisbandDrawY, kBtnW, kBtnH };
}

// Bande testée au CLIC : +100 (EA 0x666D48) — quirk binaire, cf. le bandeau du .h.
StoragePwWindow::Rect StoragePwWindow::DisbandClickRect() const {
    return { x_ + kBtnX, y_ + kBtnDisbandClickY, kBtnW, kBtnH };
}

StoragePwWindow::Rect StoragePwWindow::CloseButtonRect() const {
    return { x_ + kBtnX, y_ + kBtnCloseY, kBtnW, kBtnH };
}

// ============================================================================
// UI_StoragePwWnd_OnMouseDown 0x666A30
// ============================================================================
bool StoragePwWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x666A3B

    // [audio] Snd3D_PlayScaledVolume(flt_1487E3C, .., 100, 1) sur les 3 boutons
    // (EA 0x666AC7 / 0x666B14 / 0x666B61). Non porté : aucun registre d'émetteurs 3D
    // par adresse côté C++ (convention établie : UI/Widgets.cpp:60,
    // UI/NpcDialogWindow.cpp:287, UI/PartyWindow.cpp:307).

    const Rect leave = LeaveButtonRect();
    if (PointInRect(x, y, leave.x, leave.y, leave.w, leave.h)) {  // EA 0x666AB3 (+56)
        leaveLatch_ = true;                                       // *(this+3)=1 (EA 0x666ACF)
        return true;
    }

    // NOTE : le clic-ENFONCÉ utilise +110 (EA 0x666B00), alors que le CLIC utilise
    // +100 (EA 0x666D48). Quirk reproduit — cf. bandeau du .h.
    const Rect disband = DisbandButtonRect();
    if (PointInRect(x, y, disband.x, disband.y, disband.w, disband.h)) {
        disbandLatch_ = true;                                     // *(this+4)=1 (EA 0x666B1C)
        return true;
    }

    const Rect cls = CloseButtonRect();
    if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h)) {          // EA 0x666B4D (+164)
        closeLatch_ = true;                                       // *(this+5)=1 (EA 0x666B69)
        return true;
    }

    // Repli : `return Sprite2D_HitTest(unk_8F70D4, *this, *(this+1), a4, a5)`
    // (EA 0x666B98) — consomme le clic s'il tombe sur le fond.
    const Rect panel = PanelRect();
    return PointInRect(x, y, panel.x, panel.y, panel.w, panel.h);
}

// ============================================================================
// UI_StoragePwWnd_OnClick 0x666BB0
// ============================================================================
// Structure en if / else if EXCLUSIF sur les latches +3, +4, +5 (le binaire ne
// teste jamais deux latches dans le même appel), chaque branche désarmant son
// latch AVANT le hit-test et renvoyant 1 même si le hit-test échoue.
bool StoragePwWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x666BBB

    if (leaveLatch_) {                                          // EA 0x666C16
        leaveLatch_ = false;                                    // EA 0x666C23
        const Rect leave = LeaveButtonRect();
        if (!PointInRect(x, y, leave.x, leave.y, leave.w, leave.h))
            return true;                                        // EA 0x666C58
        HandleLeaveClick();
        return true;                                            // EA 0x666CD4
    }

    if (disbandLatch_) {                                        // EA 0x666D14
        disbandLatch_ = false;                                  // EA 0x666D21
        // QUIRK : bande de CLIC à +100 (EA 0x666D48), pas +110. Cf. le .h.
        const Rect hit = DisbandClickRect();
        if (!PointInRect(x, y, hit.x, hit.y, hit.w, hit.h))
            return true;                                        // EA 0x666D51
        HandleDisbandClick();
        return true;                                            // EA 0x666DD2
    }

    if (closeLatch_) {                                          // EA 0x666E0F
        closeLatch_ = false;                                    // EA 0x666E18
        const Rect cls = CloseButtonRect();
        if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h))
            Close();                                            // EA 0x666E55
        return true;                                            // EA 0x666E4B
    }

    return false;                                               // EA 0x666E61
}

// --- Bouton 1 : QUITTER l'alliance (réservé aux NON-leaders) ----------------
// Comparaison Crt_Strcmp = SENSIBLE À LA CASSE (EA 0x666C67 / 0x666CA8).
void StoragePwWindow::HandleLeaveClick() {
    // `!Crt_Strcmp(roster[0], &String)` -> roster[0] vide (EA 0x666C67) : LABEL_7.
    if (RosterLeader().empty()) {
        game::g_Client.msg.System(game::Str(kStrNoAlliance), SysMsgColor());   // EA 0x666C84
        return;
    }

    // `Crt_Strcmp(roster[0], byte_1673184)` != 0 -> je ne suis PAS le leader (EA 0x666CA8).
    // (Équivalent au modèle : !game::g_World.allianceRoster.IsLeader(SelfName()) — le
    // garde `!name.empty()` d'IsLeader ne change RIEN ici, roster[0] étant déjà prouvé
    // non vide par le test ci-dessus. On écrit la comparaison littérale pour rendre
    // visible l'asymétrie Strcmp/Stricmp avec le bouton 2.)
    if (RosterLeader() != SelfName()) {
        // MsgBox type 11, corps Str(364), titre = `&String` = chaîne VIDE (EA 0x666CFA).
        // FIDÉLITÉ CRITIQUE : le type 11 n'a AUCUN lecteur (data_refs(0x1822450) = 10
        // lecteurs, types 8/9/10/14/19/20/37/38 seulement) et la jumptable d'acceptation
        // 0x5C2DC3 le range dans son défaut `mov eax,1 ; retn 8`. Cliquer « Oui » ne
        // déclenche donc RIEN dans le binaire d'origine : aucun paquet n'est émis, ici
        // comme là-bas. NE PAS ajouter d'émission (cf. bandeau « FAIT DÉCISIF » du .h).
        game::g_Client.prompt.Open(kMsgBoxLeave, game::Str(kStrLeaveConfirm), std::string());
        Close();                                                               // EA 0x666D02
    } else {
        // Je SUIS le leader -> un leader ne peut pas « quitter » (EA 0x666CC4).
        game::g_Client.msg.System(game::Str(kStrLeaderCant), SysMsgColor());
    }
}

// --- Bouton 2 : DISSOUDRE l'alliance (réservé au LEADER) --------------------
// ASYMÉTRIE RÉELLE DU BINAIRE, reproduite telle quelle : ce bouton compare avec
// Crt_Stricmp (INsensible à la casse, EA 0x666D6F / 0x666DA5), alors que le
// bouton 1 utilise Crt_Strcmp (sensible). Ce n'est pas une erreur de lecture :
// les deux appels sont des symboles distincts (0x75CF20 vs 0x76668B).
void StoragePwWindow::HandleDisbandClick() {
    // `!Crt_Stricmp(roster[0], &String)` -> roster[0] vide (EA 0x666D6F) : LABEL_7.
    if (RosterLeader().empty()) {
        game::g_Client.msg.System(game::Str(kStrNoAlliance), SysMsgColor());   // EA 0x666C84
        return;
    }

    // `Crt_Stricmp(roster[0], byte_1673184)` != 0 -> je ne suis pas le leader (EA 0x666DA5).
    if (!StriEqual(RosterLeader(), SelfName())) {
        game::g_Client.msg.System(game::Str(kStrNotLeader), SysMsgColor());    // EA 0x666DC2
        return;
    }

    // MsgBox type 12, corps Str(365), titre VIDE (EA 0x666DF8) — même remarque de
    // fidélité que pour le type 11 : AUCUN consommateur, AUCUNE émission.
    game::g_Client.prompt.Open(kMsgBoxDisband, game::Str(kStrDisbandConf), std::string());
    Close();                                                                   // EA 0x666E00
}

// ============================================================================
// UI_StoragePwWnd_Render 0x666FE0
// ============================================================================
void StoragePwWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    RecomputeCenter(ctx.screenW, ctx.screenH);                  // EA 0x667000..0x66703E
    if (!bOpen_) return;                                        // EA 0x666FEB

    const Rect panel   = PanelRect();
    const Rect leave   = LeaveButtonRect();
    const Rect disband = DisbandButtonRect();                   // DESSIN à +110 (EA 0x66711E)
    const Rect cls     = CloseButtonRect();

    if (ctx.phase == UiPhase::Panels) {
        // Sprite2D_HitTest(unk_8F70D4) -> Util_SetClampedU8Field(dword_8E714C, 0)
        // (EA 0x66705B/0x66706B) : curseur slot 0 au survol du panneau. NON porté —
        // gap UTIL-01 (CursorSet::SetActiveSlot sans appelant, `cursors_` privé dans
        // App/App.h:43), correctif explicitement hors de ce front.
        // TODO [ancre 0x66706B] : Cursors().SetActiveSlot(0) au survol, une fois UTIL-01 câblé.

        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColPanelBg); // EA 0x667082
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        // FIDÉLITÉ : chaque bouton n'est dessiné QUE s'il est pressé (sprite « down » :
        // unk_8F71FC / unk_8F7324 / unk_8F744C) ou survolé (sprite « up » : unk_8F7168 /
        // unk_8F7290 / unk_8F73B8) — l'état au repos n'est PAS dessiné, son visuel
        // appartenant au sprite de FOND. Comme le fond de repli ne contient pas ces
        // visuels, on dessine le bouton au repos en teinte neutre : écart ASSUMÉ et
        // documenté (sinon les boutons seraient invisibles avec le fond de repli).
        const bool leaveHover = PointInRect(cursorX, cursorY, leave.x, leave.y, leave.w, leave.h);
        ctx.FillRect(leave.x, leave.y, leave.w, leave.h,
                     leaveLatch_ ? kColBtnDown : (leaveHover ? kColBtnHover : kColBtnBg)); // EA 0x6670F0 / 0x6670D1
        ctx.DrawFrame(leave.x, leave.y, leave.w, leave.h, kColFrame, 1);

        const bool disbandHover = PointInRect(cursorX, cursorY, disband.x, disband.y, disband.w, disband.h);
        ctx.FillRect(disband.x, disband.y, disband.w, disband.h,
                     disbandLatch_ ? kColBtnDown : (disbandHover ? kColBtnHover : kColBtnBg)); // EA 0x66715E / 0x66713F
        ctx.DrawFrame(disband.x, disband.y, disband.w, disband.h, kColFrame, 1);

        const bool clsHover = PointInRect(cursorX, cursorY, cls.x, cls.y, cls.w, cls.h);
        ctx.FillRect(cls.x, cls.y, cls.w, cls.h,
                     closeLatch_ ? kColBtnDown : (clsHover ? kColBtnHover : kColBtnBg));       // EA 0x6671D4 / 0x6671B2
        ctx.DrawFrame(cls.x, cls.y, cls.w, cls.h, kColFrame, 1);
        return;
    }

    // --- Phase texte --------------------------------------------------------
    // Le binaire n'écrit AUCUN texte : les libellés font partie des sprites de
    // boutons. Libellés de repli, écart assumé (cf. ci-dessus), formulés selon la
    // sémantique PROUVÉE des deux actions (quitter / dissoudre).
    ctx.Text("Alliance", panel.x + 8, panel.y + 6, kColTitle);

    const char* leaveLbl = "Quitter";
    ctx.Text(leaveLbl, leave.x + (leave.w - ctx.MeasureText(leaveLbl)) / 2,
             leave.y + (leave.h - 12) / 2, kColText);

    const char* disbandLbl = "Dissoudre";
    ctx.Text(disbandLbl, disband.x + (disband.w - ctx.MeasureText(disbandLbl)) / 2,
             disband.y + (disband.h - 12) / 2, kColText);

    const char* clsLbl = "Fermer";
    ctx.Text(clsLbl, cls.x + (cls.w - ctx.MeasureText(clsLbl)) / 2,
             cls.y + (cls.h - 12) / 2, kColText);
}

} // namespace ts2::ui

// UI/PartyWindow.cpp — sélecteur de membre (UI_MemberSelectWnd) + panneau HUD « Groupe ».
// Voir UI/PartyWindow.h pour le contrat, le layout prouvé de g_MemberSelectWnd 0x184BE38,
// et le bandeau « câblage manquant » (les 3 déclencheurs vivent dans des fichiers non
// possédés par cette vague).
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant
// <windows.h>, qu'UI/PartyWindow.h tire transitivement via UIManager.h -> <d3d9.h>)
// — même convention que UI/GuildWindow.cpp / UI/ChatWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 puis windows (ordre sûr)
#include "Net/NetClient.h"     // net::GlobalNetClient() (singleton g_NetClient 0x8156A0)
#include "UI/PartyWindow.h"
#include "UI/PanelSkin.h"

#include <cstdio>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit étroit/haut (252,440), le PLUS
// répété (63 occurrences non consécutives) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, retenu par
// défaut pour ce panneau HUD étroit (210 px de large, hauteur dynamique selon
// le nombre de membres ; cf. méthodologie détaillée dans UI/PanelSkin.h).
// Repli automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00472.IMG");

// Formatage sans allocation dynamique excessive (snprintf -> std::string).
std::string Fmt(const char* fmt, size_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, v);
    return std::string(buf);
}

// Map_IsArenaZone 0x54B690 : `return g_SelfMorphNpcId >= 270 && g_SelfMorphNpcId <= 274;`
// (le global 0x1675A98 est g_SelfMorphNpcId malgré le libellé « map id » du commentaire
// IDB). Porté ici plutôt que dans un en-tête partagé : cette vague ne possède que les
// fichiers PartyWindow/SocialWindow. Les autres appelants du binaire (Scene/SceneManager.cpp:934,
// Game/WarehouseSystem.h) portent encore un TODO « Map_IsArenaZone non modélisé » —
// à mutualiser dans une vague ultérieure qui possédera Game/MapWarp.h.
bool Map_IsArenaZone() {
    const int32_t morphNpcId = game::g_Client.VarGet(0x1675A98u); // g_SelfMorphNpcId
    return morphNpcId >= 270 && morphNpcId <= 274;               // EA 0x54B6BE
}

// g_SysMsgColor 0x84DFD8 — non modélisé en champ propre, longue traîne via Var()
// (même convention que Game/SocialSystem.cpp:68).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(0x84DFD8u));
}
} // namespace

// ===========================================================================
// Sélecteur de membre — UI_MemberSelectWnd (la vraie fenêtre « Groupe »)
// ===========================================================================

bool PartyWindow::MemberSelectOpen() const {
    // *(this+2) — champ +8 de g_MemberSelectWnd 0x184BE38 (garde 0x66737D / 0x66758B).
    return game::g_Client.VarGet(kVarMemberSelectOpen) != 0;
}

// UI_MemberSelectWnd_Open 0x667260.
void PartyWindow::OpenMemberSelect() {
    // --- Garde arène : refus TOTAL, message système, aucune émission (0x66726E) ---
    if (Map_IsArenaZone()) {
        // EA 0x667287 : StrTable005_Get(g_LangId, 1352) ; 0x667292 : Msg_AppendSystemLine.
        game::g_Client.msg.System(game::Str(kStrArenaRefused), SysMsgColor());
        return; // le binaire RETOURNE ici : ni bOpen, ni reset, ni Op57.
    }

    // --- bOpen = 1 (0x66729F) : c'est CE write qui ressuscite la garde du handler
    // 0x3f (Net/GameHandlers_PartyGuild.cpp:186), jamais écrite jusqu'ici. ---
    game::g_Client.Var(kVarMemberSelectOpen) = 1;

    // Latches de boutons remis à 0 (0x6672C4 : boucle i<2 sur *(this+3..4)).
    msCloseLatch_   = false;
    msConfirmLatch_ = false;

    // Sélection = -1 (0x6672D1 : *(this+5) = -1).
    game::g_Client.Var(kVarSelectedSlot) = kSlotUnset;

    // Valeurs des 10 slots = -2 (0x6672F6 : boucle j<10, *(this+j+6) = -2).
    for (int j = 0; j < kRosterSlots; ++j)
        game::g_Client.Var(kVarSlotValuesBase + 4u * static_cast<uint32_t>(j)) = kValueUnset;

    // --- Premier slot de roster NON VIDE -> Net_SendOp57(slot) (0x667300..0x667344) ---
    // Le binaire fait `return Net_SendOp57(&g_AutoPlayMgr, k)` DANS la boucle : un seul
    // envoi, sur le PREMIER slot non vide (Crt_Strcmp(g_PartyRosterNames + 13*k, "") != 0).
    // Le serveur répond par 0x3f (valeur du slot), dont le handler relance Op57 sur le
    // slot suivant : pagination une-requête-par-réponse.
    //
    // NB fidélité : `&g_AutoPlayMgr` (0x846C08) est le TAMPON d'émission, PAS le client
    // réseau — Net_SendOp57 0x4B90D0 lit la clé XOR (0x8156A4), la séquence (0x8156A5) et
    // le socket (0x8156AC) DIRECTEMENT dans g_NetClient 0x8156A0. Côté C++ le tampon est
    // interne à PacketWriter et le singleton est net::GlobalNetClient().
    const auto& names = game::g_World.partyRoster.names;
    for (int k = 0; k < kRosterSlots && k < static_cast<int>(names.size()); ++k) {
        if (names[k].empty()) continue;             // strcmp(..., "") == 0 -> slot vide
        net::NetClient* nc = net::GlobalNetClient();
        // Le test nc != nullptr n'est PAS une garde de complaisance : sur le chemin réel
        // il est TOUJOURS vrai (le roster n'est peuplé que par le handler 0x3e, donc
        // seulement une fois la session de jeu établie). Il ne couvre que l'appel
        // théorique hors session, où le binaire n'aurait de toute façon rien à émettre.
        if (nc)
            net::Net_SendOp57(*nc, static_cast<int8_t>(k)); // opcode 0x39 — EA 0x667344
        break;                                              // `return` dans la boucle
    }
}

// UI_MemberSelectWnd_Close 0x667350 : *(this+2) = 0, rien d'autre.
void PartyWindow::CloseMemberSelect() {
    game::g_Client.Var(kVarMemberSelectOpen) = 0; // EA 0x667365
}

void PartyWindow::Close() {
    // UIManager::CloseAll/ResetAll (UI_CloseAllDialogs 0x5AC590) doit refermer le
    // sélecteur : fidèle, 0x6677C4 ferme tous les dialogues avant de l'ouvrir.
    CloseMemberSelect();
    Dialog::Close();
}

// UI_MemberSelectWnd_ProcNet 0x667730 (code file UI 33) : bascule.
void PartyWindow::Toggle() {
    if (MemberSelectOpen()) {
        CloseMemberSelect();  // EA 0x66783C
        return;
    }
    // Ouverture conditionnée à l'état de scène (EA 0x667756 : g_SceneMgr == 6 &&
    // g_SceneSubState == 4 = en jeu). Le binaire ferme d'abord tous les dialogues
    // (UI_CloseAllDialogs(dword_1821D4C, 1), EA 0x6677C4) puis ouvre (EA 0x6677CC).
    // TODO [ancre 0x667756] g_SceneMgr 0x1676180 / g_SceneSubState 0x1676184 ne sont pas
    // modélisés en Var() par les fichiers possédés ici : la garde de scène n'est donc PAS
    // portée. L'appelant à câbler (barre d'outils / raccourci) est déjà, dans le binaire,
    // atteignable seulement en jeu — la garde y est redondante mais devra être rétablie
    // si Toggle() venait à être appelé depuis une autre scène.
    UIManager::Instance().CloseAll();
    OpenMemberSelect();
}

PartyWindow::MsLayout PartyWindow::ComputeMsLayout(int screenW, int screenH) const {
    // Centrage écran, recalculé à chaque événement/frame comme le binaire :
    //   *this      = nWidth/2  - Sprite2D_GetWidth(unk_90265C)/2   (EA 0x6673AD)
    //   *(this+1)  = nHeight/2 - Sprite2D_GetHeight(unk_90265C)/2  (EA 0x6673D2)
    // TODO [ancre 0x6673AD] kMsW/kMsH sont DÉDUITS : les dimensions réelles viennent du
    // sprite .IMG unk_90265C résolu au runtime, inconnu statiquement.
    MsLayout m;
    m.w = kMsW;
    m.h = kMsH;
    m.x = screenW / 2 - kMsW / 2;
    m.y = screenH / 2 - kMsH / 2;
    return m;
}

void PartyWindow::MsSlotRect(const MsLayout& m, int i, int& rx, int& ry, int& rw, int& rh) const {
    // EA 0x667438 : Sprite2D_HitTest(unk_9026F0, *this + 17, *(this+1) + 20*i + 81, ...)
    rx = m.x + kMsSlotDX;
    ry = m.y + kMsSlotDY0 + kMsSlotStep * i;
    rw = kMsSlotW; rh = kMsSlotH; // TODO [ancre 0x667438] dims déduites (sprite unk_9026F0)
}

void PartyWindow::MsCloseRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const {
    // EA 0x6674D2 : Sprite2D_HitTest(unk_8F3798, *this + 252, *(this+1) + 24, ...)
    rx = m.x + kMsCloseDX;
    ry = m.y + kMsCloseDY;
    rw = kMsCloseW; rh = kMsCloseH; // TODO [ancre 0x6674D2] dims déduites (sprite unk_8F3798)
}

void PartyWindow::MsConfirmRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const {
    // EA 0x667521 : Sprite2D_HitTest(unk_902A68, *this + 214, *(this+1) + 298, ...)
    rx = m.x + kMsOkDX;
    ry = m.y + kMsOkDY;
    rw = kMsOkW; rh = kMsOkH; // TODO [ancre 0x667521] dims déduites (sprite unk_902A68)
}

// ===========================================================================
// Panneau HUD « raid frame » (invention conservée, cf. bandeau .h)
// ===========================================================================

PartyWindow::Layout PartyWindow::BuildLayout(int screenW, int screenH) const {
    Layout L;
    (void)screenH;

    auto& client = game::g_Client;
    auto& world  = game::g_World;

    // Garde de visibilité : présence d'au moins une entité joueur connue.
    //
    // CORRECTION (vague W6) : la garde `Var(0x184BE40) != 0` qui figurait ici a été
    // RETIRÉE. Elle reposait sur une identification erronée (« flag groupe actif ») :
    // 0x184BE40 est en réalité le champ bOpen de g_MemberSelectWnd (cf. bandeau .h), il
    // n'a aucun rapport avec « le groupe est actif ». Comme personne ne l'écrivait,
    // cette garde était TOUJOURS fausse et rendait ce panneau intégralement mort. Le
    // contrat que ce panneau se donne (« visible tant qu'au moins un membre est résolu »)
    // est déjà assuré par le test `src.empty()` plus bas — c'est la seule garde légitime.
    if (world.players.empty()) return L; // L.visible reste false

    struct RowSrc {
        std::string name;
        int hp = 0, hpMax = 0;
        int mp = 0, mpMax = 0;
        bool hasMp = false;
    };
    std::vector<RowSrc> src;
    src.reserve(kMaxRows);

    // --- Soi (toujours en tête si présent, source réelle SelfState -> StatEngine) ---
    if (world.players[0].active) {
        RowSrc r;
        r.name   = "Moi";
        r.hp     = world.self.hp;
        r.hpMax  = world.self.maxHp;
        r.mp     = world.self.mp;
        r.mpMax  = world.self.maxMp;
        r.hasMp  = true;
        src.push_back(std::move(r));
    }

    // --- Autres membres : un slot du tableau joueurs est traité comme « membre de
    // groupe résolu » si PartyMemberHpSet/PartyMemberUpdate (opcodes 0x7f/0x80) a
    // déjà écrit une PV MAX non nulle pour ce slot. Ces deux opcodes sont émis par
    // le serveur UNIQUEMENT pour de véritables membres de groupe (contrairement à
    // world.players[], qui contient TOUT joueur visible à proximité) — c'est donc
    // le meilleur signal disponible pour ne pas afficher des inconnus. ---
    for (size_t i = 1; i < world.players.size() && src.size() < static_cast<size_t>(kMaxRows); ++i) {
        if (!world.players[i].active) continue;
        const uint32_t addr = static_cast<uint32_t>(kMemberStride * i);
        const int hpMax = client.VarGet(kVarMemberHpMaxBase + addr);
        if (hpMax <= 0) continue; // aucune donnée de groupe reçue pour ce slot

        RowSrc r;
        // Nom réel : g_PartyRosterNames (game::g_World.partyRoster.names), peuplé par
        // Net_OnPartyMemberNameSet/_Clear (opcodes 0x3e/0x40, Net/GameHandlers_PartyGuild.cpp).
        // ATTENTION (best-effort, cf. Game/GameState.h::PartyRoster) : `i` est ici un index
        // d'ENTITÉ (world.players, résolu par identité réseau via PartyMemberHpSet/Update),
        // alors que le roster de noms est indexé par un slot ASSIGNÉ PAR LE SERVEUR sans lien
        // prouvé avec l'index d'entité. On lit quand même names[i] en repli le plus probable
        // (aucune clé de jointure connue dans le désassemblage) ; si ce slot est vide/pas
        // encore reçu, on retombe sur un libellé générique plutôt que d'inventer un nom.
        // NB : cette jointure douteuse ne contamine PAS le chemin réseau — Op57/Op58
        // transportent un index de ROSTER, lu directement dans partyRoster.names.
        const std::string& rosterName =
            (i < world.partyRoster.names.size()) ? world.partyRoster.names[i] : std::string();
        r.name = !rosterName.empty() ? rosterName : "Membre";
        r.hp    = client.VarGet(kVarMemberHpBase + addr);
        r.hpMax = hpMax;
        r.hasMp = false; // PartyMemberHpSet écrit la MÊME adresse pour kind==1 (PV)
                          // et kind==2 (PM) : aucune adresse distincte pour le PM
                          // des coéquipiers dans l'état actuel du handler.
        src.push_back(std::move(r));
    }

    if (src.empty()) return L; // aucun membre résolu -> masqué

    // --- Géométrie (ancré haut-gauche, indépendant de la résolution écran) ---
    L.visible = true;
    L.x = kMarginX;
    L.y = kMarginY;
    L.w = kPanelW;

    const int barW  = kPanelW - 2 * kPadX;
    const int rowH  = kNameH + kBarGapY + kBarH + kBarGapY + kBarH + kRowGapY;
    int ty = kPadY + kTitleH + 4;

    for (const auto& s : src) {
        RowLayout rl;
        rl.name   = s.name;
        rl.nameY  = L.y + ty;
        rl.hp     = s.hp;
        rl.hpMax  = s.hpMax;
        rl.mp     = s.mp;
        rl.mpMax  = s.mpMax;
        rl.hasMp  = s.hasMp;

        rl.hpX = L.x + kPadX; rl.hpY = L.y + ty + kNameH + kBarGapY; rl.hpW = barW; rl.hpH = kBarH;
        rl.mpX = rl.hpX;      rl.mpY = rl.hpY + kBarH + kBarGapY;    rl.mpW = barW; rl.mpH = kBarH;

        L.rows.push_back(std::move(rl));
        ty += rowH;
    }

    L.h = ty - kRowGapY + kPadY;
    (void)screenW;
    return L;
}

// ===========================================================================
// Routage
// ===========================================================================

// UI_MemberSelectWnd_OnMouseDown 0x667370.
bool PartyWindow::OnMouseDown(int x, int y) {
    if (MemberSelectOpen()) {
        // Recentrage AVANT hit-test, comme le binaire (0x667394..0x6673D2).
        const MsLayout m = ComputeMsLayout(lastScreenW_, lastScreenH_);

        const auto& names = game::g_World.partyRoster.names;

        // --- 10 slots de noms : seuls les slots NON VIDES sont cliquables (0x667438) ---
        for (int i = 0; i < kRosterSlots && i < static_cast<int>(names.size()); ++i) {
            if (names[i].empty()) continue;
            int rx, ry, rw, rh;
            MsSlotRect(m, i, rx, ry, rw, rh);
            if (!PointInRect(x, y, rx, ry, rw, rh)) continue;

            // Déjà sélectionné -> consommé, aucun effet (0x66744A).
            if (i == game::g_Client.VarGet(kVarSelectedSlot)) return true;

            // 0x667461 : Snd3D_PlayScaledVolume — son non porté (couche audio).
            game::g_Client.Var(kVarSelectedSlot) = i; // 0x66746C

            // 0x66747A : si la valeur du slot est > 0 -> g_WhisperPresetSlot = 0 (0x66747C)
            // puis Crt_StringInit() (0x667498 : vide la chaîne de chuchotement associée —
            // buffer non modélisé ici, seul le flag l'est).
            if (game::g_Client.VarGet(kVarSlotValuesBase + 4u * static_cast<uint32_t>(i)) > 0)
                game::g_Client.Var(kVarWhisperPreset) = 0;
            return true; // 0x667577
        }

        // --- Boutons (atteints seulement si AUCUN slot n'a été touché, i >= 10) ---
        int bx, by, bw, bh;
        MsCloseRect(m, bx, by, bw, bh);
        if (PointInRect(x, y, bx, by, bw, bh)) {
            msCloseLatch_ = true;  // *(this+3) = 1 — 0x6674EE
            return true;
        }
        MsConfirmRect(m, bx, by, bw, bh);
        if (PointInRect(x, y, bx, by, bw, bh)) {
            msConfirmLatch_ = true; // *(this+4) = 1 — 0x66753D
            return true;
        }
        // Clic ailleurs : consommé si dans le panneau (0x66756C), sinon non.
        if (PointInRect(x, y, m.x, m.y, m.w, m.h)) return true;
    }

    // --- Raid frame : consomme uniquement le clic tombant SUR le panneau dessiné
    // (évite le clic-traversant vers le monde 3D). Panneau d'information pure. ---
    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

// UI_MemberSelectWnd_OnClick 0x667580.
bool PartyWindow::OnClick(int x, int y) {
    if (MemberSelectOpen()) {
        // Recentrage AVANT hit-test (0x6675A2..0x6675E0).
        const MsLayout m = ComputeMsLayout(lastScreenW_, lastScreenH_);

        // --- Bouton « Fermer » (latch *(this+3)) — 0x6675E6 ---
        if (msCloseLatch_) {
            msCloseLatch_ = false;                    // 0x6675EF
            int bx, by, bw, bh;
            MsCloseRect(m, bx, by, bw, bh);
            if (PointInRect(x, y, bx, by, bw, bh))
                CloseMemberSelect();                  // 0x66762F
            return true;                              // consommé même si le hit échoue (0x667622)
        }

        // --- Bouton « Valider » (latch *(this+4)) — 0x667641 ---
        if (msConfirmLatch_) {
            msConfirmLatch_ = false;                  // 0x66764E
            int bx, by, bw, bh;
            MsConfirmRect(m, bx, by, bw, bh);
            if (!PointInRect(x, y, bx, by, bw, bh)) return true; // 0x667683

            if (game::g_Client.VarGet(kVarSelectedSlot) == kSlotUnset) {
                // Aucun membre sélectionné : message système, AUCUNE émission (0x667691).
                game::g_Client.msg.System(game::Str(kStrNoSelection), SysMsgColor()); // 0x6676AF
            } else {
                // UI_MsgBox_Open(dword_1822438, 21, StrTable005_Get(g_LangId, 530), "")
                // — EA 0x6676D7. Le contextType 21 du binaire (this+24) est remplacé
                // côté C++ par le callback de résultat ci-dessous.
                UIManager::Instance().MsgBox().Open(
                    game::Str(kStrConfirmBody), std::string(),
                    [](int button) {
                        // UI_MsgBox_OnLButtonUp 0x5C0A90.
                        // OK -> jpt_5C0BE5 case 21 = 0x5C11E9.
                        // Annuler -> jpt_5C2DC3 : case 21 tombe dans def_5C2DC3
                        // (« default case, cases 4-7,11-13,15-18,21-36 ») = AUCUNE
                        // émission, aucun effet — vérifié au désassemblage.
                        if (button != MsgBoxDialog::kBtnOk) return;

                        const int32_t slot = game::g_Client.VarGet(kVarSelectedSlot); // 0x5C11E9
                        net::NetClient* nc = net::GlobalNetClient();
                        // Non-garde de complaisance : cette boîte ne peut s'ouvrir que
                        // depuis un sélecteur ouvert en session de jeu, où nc est établi.
                        if (nc)
                            net::Net_SendOp58(*nc, static_cast<int8_t>(slot)); // opcode 0x3A — 0x5C11F5
                        // Reset APRÈS l'envoi, inconditionnel (ordre exact 0x5C11F5 -> 0x5C11FA).
                        game::g_Client.Var(kVarSelectedSlot) = kSlotUnset;
                    },
                    true /* withCancel : la boîte 21 a bien une branche Annuler (no-op) */);
            }
            return true; // 0x6676B4
        }

        // Aucun latch armé : NON consommé (0x6676E3) — le binaire renvoie 0 ici.
    }

    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

// ===========================================================================
// Rendu
// ===========================================================================

void PartyWindow::RenderMemberSelect(const UiContext& ctx, int cursorX, int cursorY) {
    (void)cursorX; (void)cursorY;

    // UI_MemberSelectWnd_Render 0x667860 : recentrage écran à chaque frame.
    const MsLayout m = ComputeMsLayout(ctx.screenW, ctx.screenH);

    const auto& names = game::g_World.partyRoster.names;
    const int32_t selected = game::g_Client.VarGet(kVarSelectedSlot);

    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(m.x, m.y, m.w, m.h, kColBg);
        ctx.DrawFrame(m.x, m.y, m.w, m.h, kColBorder, 1);

        for (int i = 0; i < kRosterSlots && i < static_cast<int>(names.size()); ++i) {
            if (names[i].empty()) continue; // slots vides non cliquables (0x667438)
            int rx, ry, rw, rh;
            MsSlotRect(m, i, rx, ry, rw, rh);
            ctx.FillRect(rx, ry, rw, rh, (i == selected) ? kColSelBg : kColSlotBg);
            ctx.DrawFrame(rx, ry, rw, rh, kColBorder, 1);
        }

        int bx, by, bw, bh;
        MsCloseRect(m, bx, by, bw, bh);
        ctx.FillRect(bx, by, bw, bh, kColBtnBg);
        ctx.DrawFrame(bx, by, bw, bh, kColBorder, 1);

        MsConfirmRect(m, bx, by, bw, bh);
        ctx.FillRect(bx, by, bw, bh, kColBtnBg);
        ctx.DrawFrame(bx, by, bw, bh, kColBorder, 1);
        return;
    }

    // Phase texte. Libellés : le binaire les tire de StrTable005 via des identifiants
    // non relevés pour ce panneau — game::Str() rend un placeholder stable « #<id> »
    // tant que la table n'est pas déchiffrée, donc on s'en tient à des libellés neutres.
    // TODO [ancre 0x667860] identifiants StrTable005 du titre et des boutons non relevés.
    const char* title = "Groupe";
    ctx.Text(title, m.x + (m.w - ctx.MeasureText(title)) / 2, m.y + 24, kColTitle);

    for (int i = 0; i < kRosterSlots && i < static_cast<int>(names.size()); ++i) {
        if (names[i].empty()) continue;
        int rx, ry, rw, rh;
        MsSlotRect(m, i, rx, ry, rw, rh);
        ctx.Text(names[i].c_str(), rx + 4, ry + 2, kColText);
    }

    int bx, by, bw, bh;
    MsCloseRect(m, bx, by, bw, bh);
    ctx.Text("X", bx + 6, by + 3, kColText);

    MsConfirmRect(m, bx, by, bw, bh);
    ctx.Text("OK", bx + 18, by + 4, kColText);
}

void PartyWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    (void)cursorX; (void)cursorY;

    const Layout L = BuildLayout(ctx.screenW, ctx.screenH);

    // bOpen_/x_/y_ (champs protégés de Dialog) reflètent l'état auto-masqué,
    // recalculé à CHAQUE Render (les deux phases donnent le même résultat dans la
    // même frame). Le sélecteur modal compte lui aussi comme « ouvert » : IsOpen()
    // doit être vrai tant qu'il est affiché (UIManager n'ouvre/ferme rien de lui-même,
    // mais les appelants externes testent IsOpen()).
    const bool msOpen = MemberSelectOpen();
    bOpen_ = L.visible || msOpen;
    x_ = L.x;
    y_ = L.y;

    lastVisible_ = L.visible;
    lastX_ = L.x; lastY_ = L.y; lastW_ = L.w; lastH_ = L.h;

    // Dims écran pour le hit-test inter-frames du sélecteur (cf. .h) : le binaire
    // recentre à chaque événement à partir de nWidth/nHeight courants.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;

    auto BarFill = [](int cur, int max) -> float {
        if (max <= 0) return 0.0f;
        float t = static_cast<float>(cur) / static_cast<float>(max);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return t;
    };

    if (L.visible) {
        if (ctx.phase == UiPhase::Panels) {
            kPanelBg.Draw(ctx, L.x, L.y, L.w, L.h, kColBg);
            ctx.DrawFrame(L.x, L.y, L.w, L.h, kColBorder, 1);

            for (const auto& r : L.rows) {
                // Barre PV (toujours des données réelles pour les lignes affichées).
                ctx.FillRect(r.hpX, r.hpY, r.hpW, r.hpH, kColHpBg);
                const int hpFillW = static_cast<int>(r.hpW * BarFill(r.hp, r.hpMax));
                if (hpFillW > 0) ctx.FillRect(r.hpX, r.hpY, hpFillW, r.hpH, kColHpFill);
                ctx.DrawFrame(r.hpX, r.hpY, r.hpW, r.hpH, kColBorder, 1);

                // Barre PM : grisée/vide si aucune donnée réelle (cf. bandeau .h).
                if (r.hasMp) {
                    ctx.FillRect(r.mpX, r.mpY, r.mpW, r.mpH, kColMpBg);
                    const int mpFillW = static_cast<int>(r.mpW * BarFill(r.mp, r.mpMax));
                    if (mpFillW > 0) ctx.FillRect(r.mpX, r.mpY, mpFillW, r.mpH, kColMpFill);
                } else {
                    ctx.FillRect(r.mpX, r.mpY, r.mpW, r.mpH, kColNoData);
                }
                ctx.DrawFrame(r.mpX, r.mpY, r.mpW, r.mpH, kColBorder, 1);
            }
        } else {
            const char* title = "Groupe";
            ctx.Text(title, L.x + (L.w - ctx.MeasureText(title)) / 2, L.y + kPadY, kColTitle);

            for (const auto& r : L.rows) {
                ctx.Text(r.name.c_str(), L.x + kPadX, r.nameY, kColText);
            }
        }
    }

    // Le sélecteur modal se dessine PAR-DESSUS le raid frame (même dialogue, donc même
    // rang de rendu). Cf. bandeau .h : party_ étant enregistré en fin de liste
    // (UI/GameWindows.cpp:59), ce modal passe sous les autres fenêtres — à arbitrer par
    // l'orchestrateur (fichier non possédé).
    if (msOpen) RenderMemberSelect(ctx, cursorX, cursorY);
}

} // namespace ts2::ui

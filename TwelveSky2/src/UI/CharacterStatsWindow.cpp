// UI/CharacterStatsWindow.cpp — implémentation de la fiche personnage.
// Voir UI/CharacterStatsWindow.h pour les références de RE (StatFormulas.h,
// GameState.h, émission Net_SendVaultReq_206 0x590430 -> opcode 0x13 / sous-code 206).
#include "UI/CharacterStatsWindow.h"
#include "UI/PanelSkin.h"
// Émission de la dépense de points d'attribut (cDrawWin_OnCommit 0x6291F0) :
// Net_SendVaultReq_206 (Net/SendPackets.h) + singleton g_NetClient 0x8156A0 restauré
// par net::GlobalNetClient() (Net/NetClient.h) + verrous g_GmCmdCooldownLatch
// 0x1675B08 / g_MorphInProgress 0x1675A88 / flt_1675B0C 0x1675B0C (Net/ClientState.h).
// NB : Net/SendPackets.h inclut déjà NetClient.h et ClientState.h.
#include "Net/SendPackets.h"

#include <cstdio>

namespace ts2::ui {

// ===========================================================================
// Palette (ARGB, D3DCOLOR = 0xAARRGGBB) — cf. contrat UI.
// ===========================================================================
namespace {

// Fond de panneau réel (best effort) : gabarit (446,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec la fiche personnage (480x380 ; cf. méthodologie
// détaillée dans UI/PanelSkin.h). Indice distinct de celui utilisé par
// GuildWindow (même cluster de tailles, fichiers différents). Repli
// automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01338.IMG");

constexpr D3DCOLOR kColBg        = Argb(0xE0, 0x20, 0x20, 0x28); // fond panneau
constexpr D3DCOLOR kColTitleBg   = Argb(0xF0, 0x18, 0x18, 0x20); // bandeau titre (plus sombre)
constexpr D3DCOLOR kColFrame     = Argb(0xFF, 0x80, 0x80, 0x80); // cadre
constexpr D3DCOLOR kColText      = Argb(0xFF, 0xFF, 0xFF, 0xFF); // texte normal
constexpr D3DCOLOR kColTitle     = Argb(0xFF, 0xFF, 0xDD, 0x66); // titre
constexpr D3DCOLOR kColLabel     = Argb(0xFF, 0xC0, 0xC0, 0xC8); // libellés (gris clair)
constexpr D3DCOLOR kColHover     = Argb(0xFF, 0x40, 0x60, 0xA0); // survol
constexpr D3DCOLOR kColBtn       = Argb(0xFF, 0x38, 0x40, 0x50); // bouton normal
constexpr D3DCOLOR kColBtnDown   = Argb(0xFF, 0x58, 0x84, 0xC8); // bouton enfoncé
constexpr D3DCOLOR kColHp        = Argb(0xFF, 0xE0, 0x40, 0x40); // teinte "Vie"
constexpr D3DCOLOR kColMp        = Argb(0xFF, 0x40, 0x60, 0xE0); // teinte "Mana"
constexpr D3DCOLOR kColUnspent   = Argb(0xFF, 0x80, 0xE0, 0x80); // points non dépensés (vert)
constexpr D3DCOLOR kColDivider   = Argb(0xFF, 0x50, 0x50, 0x58); // ligne de séparation

// --- Constantes de géométrie ---
constexpr int kBoxW      = 480;
constexpr int kBoxH      = 380;
constexpr int kTitleH    = 28;
constexpr int kCloseSize = 18;
constexpr int kRowH      = 24;
constexpr int kPlusSize  = 18;

// Grille 2x2 des attributs primaires — RÉELLE (cDrawWin_Draw 0x629C9E/0x629D66,
// cf. bandeau CONFIRME_FIDELE du .h) : valeurs alignées à droite en (+107,+110)
// et (+203,+110)/(+107,+132)/(+203,+132) depuis l'origine panneau.
constexpr int kAttrRowY0      = 110; // ligne haute (ExtForce / IntForce)
constexpr int kAttrRowY1      = 132; // ligne basse (Defensive / Offensive)
constexpr int kAttrValueColX0 = 107; // colonne gauche (bord droit du texte valeur)
constexpr int kAttrValueColX1 = 203; // colonne droite

constexpr int kStatsStartYOff = 200; // depuis box.y (sous le séparateur) — non ré-audité cette passe
} // namespace

// ===========================================================================
// Libellés / accès attributs primaires
// ===========================================================================
const char* CharacterStatsWindow::AttrLabel(PrimaryAttr a) {
    switch (a) {
        case PrimaryAttr::ExtForce:  return "Force Externe";
        case PrimaryAttr::IntForce:  return "Force Interne";
        case PrimaryAttr::Defensive: return "Défensif";
        case PrimaryAttr::Offensive: return "Offensif";
    }
    return "?";
}

int CharacterStatsWindow::AttrValue(const game::SelfState& s, PrimaryAttr a) {
    switch (a) {
        case PrimaryAttr::ExtForce:  return s.attrExtForce;
        case PrimaryAttr::IntForce:  return s.attrIntForce;
        case PrimaryAttr::Defensive: return s.attrDefensive;
        case PrimaryAttr::Offensive: return s.attrOffensive;
    }
    return 0;
}

// ===========================================================================
// Layout — ANCRAGE PROPORTIONNEL réel (CONFIRME_FIDELE, cf. bandeau du .h) :
// PAS un centrage écran. Reproduit exactement UI_ProjectSpriteToScreen 0x50F5D0
// tel qu'appelé par cDrawWin_Draw/cDrawWin_OnMouseDown (0x6299AA/0x628ED0) :
//   x = round((kDesignAnchorX + w/2) * screenW / kRefWidth)  - w/2
//   y = round((kDesignAnchorY + h/2) * screenH / kRefHeight) - h/2
// où w/h = dimensions du panneau (kBoxW/kBoxH ; réelles non confirmées, cf. .h).
// À la résolution de référence (kRefWidth x kRefHeight), se réduit exactement à
// (x,y) = (kDesignAnchorX, kDesignAnchorY) = (115,105) — coin HAUT-GAUCHE de
// l'écran, pas le centre.
// ===========================================================================
void CharacterStatsWindow::ComputeLayout(int screenW, int screenH, Layout& L) const {
    L.box.w = kBoxW;
    L.box.h = kBoxH;

    // Formule identique à UI_ProjectSpriteToScreen 0x50F5D0 (ancre le CENTRE du
    // panneau à la même fraction d'écran que sa position de conception, taille
    // pixel du panneau non mise à l'échelle).
    const long long centerXNum = static_cast<long long>(kDesignAnchorX + kBoxW / 2) * screenW;
    const long long centerYNum = static_cast<long long>(kDesignAnchorY + kBoxH / 2) * screenH;
    const int centerX = static_cast<int>(centerXNum / ts2::kRefWidth);
    const int centerY = static_cast<int>(centerYNum / ts2::kRefHeight);
    L.box.x = centerX - kBoxW / 2;
    L.box.y = centerY - kBoxH / 2;

    L.titleBar = Rect{ L.box.x, L.box.y, L.box.w, kTitleH };

    // Bouton fermeture réel : offset fixe (8,6) depuis l'origine panneau
    // (HAUT-GAUCHE), cf. cDrawWin_OnMouseDown 0x629188.
    L.closeBtn = Rect{ L.box.x + kCloseOffX, L.box.y + kCloseOffY,
                        kCloseSize, kCloseSize };

    // Boutons "+1" et "+5" réels : deux grilles 2x2 fixes (PAS une colonne), mêmes
    // lignes (kPlusOffY), colonnes distinctes (kPlusOffX / kPlus5OffX) — cf. bandeau
    // du .h (cDrawWin_OnMouseDown 0x628F02.. pour "+1", 0x62904D.. pour "+5").
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        L.plusBtn[i]  = Rect{ L.box.x + kPlusOffX[col],  L.box.y + kPlusOffY[row],
                              kPlusSize, kPlusSize };
        L.plus5Btn[i] = Rect{ L.box.x + kPlus5OffX[col], L.box.y + kPlusOffY[row],
                              kPlusSize, kPlusSize };
    }
}

// ===========================================================================
// Émission — bloc commun aux 8 boutons ("+1" args 1..4 / "+5" args 5..8).
//
// Reproduit à l'identique le corps de chaque branche de cDrawWin_OnCommit 0x6291F0.
// Branche de référence (« +1 Force Externe », arg=1) :
//     if (g_MorphInProgress == 1) return 1;        // 0x629276 — refus MUET
//     if (g_GmCmdCooldownLatch)   return 1;        // 0x629289 — refus MUET
//     Net_SendVaultReq_206(1);                     // 0x62929C
//     g_GmCmdCooldownLatch = 1;                    // 0x6292A1
//     flt_1675B0C = g_GameTimeSec;                 // 0x6292B1
//     --g_SelfUnspentAttrPoints;                   // 0x6292C0
// Les 7 autres branches sont identiques au couple (arg, coût) près :
//   arg 2 -> 0x62934A/0x62936E   arg 3 -> 0x6293F8/0x62941C   arg 4 -> 0x6294A9/0x6294CD
//   arg 5 -> 0x629554/0x629578   arg 6 -> 0x629602/0x629626   arg 7 -> 0x6296B0/0x6296D4
//   arg 8 -> 0x629761/0x629785   (args 5..8 : g_SelfUnspentAttrPoints -= 5)
//
// EFFET LOCAL OPTIMISTE — VOULU ET PROUVÉ : le décrément de g_SelfUnspentAttrPoints
// (0x16731D0 == SelfState::unspentAttr) est bien fait ICI, immédiatement, dans le même
// bloc que l'envoi. Le commentaire de la passe précédente (« On NE décrémente PAS
// self.unspentAttr optimistiquement ») était FAUX vis-à-vis du binaire ; il n'était
// cohérent que tant qu'on n'émettait rien. Les VALEURS d'attribut, elles, ne sont PAS
// touchées localement : elles reviennent du serveur (Net_OnCultivationDispatch 0x493180
// / Pkt_CharStatDelta 0x465D90), lequel remet aussi g_GmCmdCooldownLatch à 0 (cf.
// Net/GameHandlers_Misc.cpp, dispatch 0x58) — c'est ce qui déverrouille le bouton.
//
// Le binaire NE re-teste PAS g_SelfUnspentAttrPoints ici : le test (> 0 pour "+1",
// >= 5 pour "+5") n'a lieu qu'à l'ARMEMENT (cDrawWin_OnMouseDown 0x628EDC / 0x629027).
// On ne l'ajoute donc pas non plus, y compris dans le cas limite où le compteur
// tomberait à 0 entre l'enfoncement et le relâchement (le binaire enverrait et
// décrémenterait quand même).
// ===========================================================================
void CharacterStatsWindow::CommitAttrSpend(int arg, int cost) {
    if (net::g_MorphInProgress == 1) return; // 0x629276 — morph en cours : refus muet
    if (net::g_GmCmdCooldownLatch)  return; // 0x629289 — requête déjà en vol : refus muet

    // g_NetClient 0x8156A0 est un GLOBAL dans le binaire : Net_SendVaultReq_206
    // (0x590430) ne reçoit aucun socket, il appelle Net_SendPacket_Op19(&g_AutoPlayMgr,
    // 206, &arg) qui adresse le client réseau directement. Côté C++ le singleton est
    // restauré par net::GlobalNetClient() (Net/NetClient.h:67-68), renseigné par
    // ConnectGameServer (Net/Login.cpp). Ce chemin est réellement atteint : la fiche
    // personnage n'est ouvrable qu'une fois entré dans le monde, donc bien après le
    // handshake -> le pointeur est non nul (ce n'est PAS un `if (nc)` mort).
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return; // hors session : pas d'envoi -> donc pas de décrément non plus

    // Sous-code 206, payload[0..3] = arg int32 LE (le builder promeut le char sur
    // 4 octets, cf. Crt_Memcpy(v2, &a1, 4u) 0x590454).
    net::Net_SendVaultReq_206(*nc, static_cast<int8_t>(arg)); // 0x62929C
    net::g_GmCmdCooldownLatch = 1;                            // 0x6292A1
    // TODO [ancre 0x815180] horodatage écrit avec net::g_GameTimeSec (Net/ClientState.h:12)
    // par COHÉRENCE avec tous les autres émetteurs gardés (Net_SendGuarded_* utilisent ce
    // même symbole)... mais ce symbole est un STUB jamais alimenté (toujours 0.0f). Le
    // binaire n'a qu'UN g_GameTimeSec (flt_815180) ; la réécriture en a DEUX qui
    // prétendent tous deux le modéliser : net::g_GameTimeSec (mort) et gfx::g_GameTimeSec
    // (Gfx/SpriteBatch.cpp:11, VRAIMENT alimenté par App.cpp:630 = gameClockSec_).
    // flt_1675B0C part donc à 0 au lieu de l'horloge de jeu. Sans effet sur CE front (le
    // déverrouillage du bouton vient du latch, remis à 0 par le dispatch 0x58 entrant, pas
    // d'un timeout), mais flt_1675B0C est LU ailleurs (AutoPlay_CheckReturnScroll 0x45C8E1,
    // Game_OnHotkey 0x537B7E, Npc_AutoSelectNearest 0x53AD47…). Correctif = fusionner les
    // deux symboles dans Net/ClientState.h — fichier NON possédé par ce front, remonté au
    // rapport.
    net::flt_1675B0C = net::g_GameTimeSec;                    // 0x6292B1
    game::g_World.self.unspentAttr -= cost;                   // 0x6292C0 (-1) / 0x629578 (-5)
}

// ===========================================================================
// Cycle de vie
// ===========================================================================
void CharacterStatsWindow::Open() {
    Dialog::Open();
    closeArmed_ = false;
    for (bool& b : plusArmed_)  b = false;
    for (bool& b : plus5Armed_) b = false;
}

// ===========================================================================
// Souris
// ===========================================================================
// Armement — ORDRE EXACT de cDrawWin_OnMouseDown 0x628EA0 : les 4 "+1" d'abord
// (gate g_SelfUnspentAttrPoints > 0, 0x628EDC), puis les 4 "+5" (gate >= 5, 0x629027 :
// `cmp` puis saut par-dessus les 4 tests), puis le bouton fermeture (0x629188), puis
// le fond de panneau (0x6291D3). L'ordre importe : les rectangles "+1"/"+5" se
// chevauchent de 3 px avec kPlusSize=18 (cf. TODO du .h) — tester "+1" en premier
// attribue la zone commune au "+1", comme le binaire.
// NB : le binaire joue aussi un son de clic à chaque armement (Snd3D_PlayScaledVolume
// (flt_1487E3C, 0, 100, 1), 0x628F16 & suivants) — non reproduit ici, cette fenêtre
// n'a aucun accès audio (hors périmètre du front, aucun paquet en jeu).
bool CharacterStatsWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false; // *(this+2) == 0 -> return 0 (0x628EAA)

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    const game::SelfState& self = game::g_World.self;

    if (self.unspentAttr > 0) { // 0x628EDC
        for (int i = 0; i < kPrimaryAttrCount; ++i) {
            const Rect& r = L.plusBtn[i];
            if (PointInRect(x, y, r.x, r.y, r.w, r.h)) { // 0x628F02..0x628FF3
                plusArmed_[i] = true;                     // *(this+3..+6) = 1
                return true;
            }
        }
    }

    if (self.unspentAttr >= 5) { // 0x629027 (`cmp g_SelfUnspentAttrPoints, 5` / `jl`)
        for (int i = 0; i < kPrimaryAttrCount; ++i) {
            const Rect& r = L.plus5Btn[i];
            if (PointInRect(x, y, r.x, r.y, r.w, r.h)) { // 0x62904D..0x62913E
                plus5Armed_[i] = true;                    // *(this+7..+10) = 1
                return true;
            }
        }
    }

    if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) { // 0x629188
        closeArmed_ = true;                                                           // *(this+11) = 1
        return true;
    }

    // Clic n'importe où ailleurs dans le panneau : consommé (empêche le clic de
    // "traverser" jusqu'au monde 3D derrière la fenêtre) mais n'arme rien — le binaire
    // renvoie ici le hit-test du sprite de fond unk_8F3704 (0x6291D3).
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

// Validation — chaîne if/else-if de cDrawWin_OnCommit 0x6291F0, dans son ordre exact :
// *(this+3..+6) = "+1" (args 1..4), *(this+7..+10) = "+5" (args 5..8), *(this+11) =
// fermeture. Le binaire ne traite qu'UN seul latch par appel (chaîne else-if) et
// désarme le latch testé AVANT de re-hit-tester ; le clic est consommé (return 1) que
// le relâchement retombe sur le bouton ou non (0x629265 vs 0x62928B).
// Le mapping bouton -> arg est PROUVÉ par la géométrie : cDrawWin_Draw dessine la
// valeur de chaque attribut juste à côté de son bouton — Char_SumAttrField292/296/
// 300/304 en (107,110)/(203,110)/(107,132)/(203,132) (0x629C76/0x629CD7/0x629D3B/
// 0x629D9F), soit l'ordre PrimaryAttr {ExtForce=0, IntForce=1, Defensive=2,
// Offensive=3} mappé en grille (col=i%2, row=i/2) => arg = i+1 ("+1") / i+5 ("+5").
bool CharacterStatsWindow::OnClick(int x, int y) {
    if (!bOpen_) return false; // *(this+2) == 0 -> return 0 (0x6291FA)

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    // "+1" : args 1..4, coût 1 (émissions 0x62929C/0x62934A/0x6293F8/0x6294A9).
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        if (!plusArmed_[i]) continue;
        plusArmed_[i] = false;                              // *(this+3..+6) = 0 (0x629235)
        const Rect& r = L.plusBtn[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h))          // 0x62925C — re-hit-test au relâchement
            CommitAttrSpend(i + 1, 1);
        return true;                                        // consommé dans les deux cas
    }

    // "+5" : args 5..8, coût 5 (émissions 0x629554/0x629602/0x6296B0/0x629761).
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        if (!plus5Armed_[i]) continue;
        plus5Armed_[i] = false;                             // *(this+7..+10) = 0 (0x6294ED)
        const Rect& r = L.plus5Btn[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h))          // 0x629514
            CommitAttrSpend(i + 5, 5);
        return true;
    }

    if (closeArmed_) {                                      // *(this+11) (0x629795)
        closeArmed_ = false;                                // 0x62979E
        if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) // 0x6297C5
            Close();                                        // cDrawWin_Close 0x628E80
        return true;                                        // 0x6297CE
    }

    // Relâché n'importe où dans le panneau : consommé.
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;                                           // 0x6297E4
}

bool CharacterStatsWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// ===========================================================================
// Rendu
// ===========================================================================
void CharacterStatsWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Mémorise les dims écran courantes pour que le hit-test (routé entre deux
    // frames) s'aligne sur la géométrie effectivement dessinée. Fait dans les
    // deux sous-passes (Panels puis Text), comme MsgBoxDialog.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Layout L;
    ComputeLayout(ctx.screenW, ctx.screenH, L);

    const game::SelfState& self = game::g_World.self;
    // Gates de dessin des deux jeux de boutons, identiques aux gates d'armement :
    // "+1" si g_SelfUnspentAttrPoints > 0, "+5" si >= 5 (cDrawWin_Draw 0x62A3D3,
    // cDrawWin_OnMouseDown 0x628EDC / 0x629027).
    const bool hasPoints  = self.unspentAttr > 0;
    const bool hasPoints5 = self.unspentAttr >= 5;

    char buf[96];

    if (ctx.phase == UiPhase::Panels) {
        // --- Fond + cadre + bandeau de titre ---
        kPanelBg.Draw(ctx, L.box.x, L.box.y, L.box.w, L.box.h, kColBg);
        ctx.FillRect(L.titleBar.x, L.titleBar.y, L.titleBar.w, L.titleBar.h, kColTitleBg);
        ctx.DrawFrame(L.box.x, L.box.y, L.box.w, L.box.h, kColFrame, 2);
        ctx.FillRect(L.box.x, L.box.y + kTitleH, L.box.w, 1, kColDivider);

        // --- Bouton fermeture ---
        const bool closeHover = PointInRect(cursorX, cursorY, L.closeBtn.x, L.closeBtn.y,
                                            L.closeBtn.w, L.closeBtn.h);
        const D3DCOLOR closeCol = closeArmed_ ? kColBtnDown : (closeHover ? kColHover : kColBtn);
        ctx.FillRect(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, closeCol);
        ctx.DrawFrame(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, kColFrame, 1);

        // --- Séparateur avant les stats dérivées ---
        const int sepY = L.box.y + kStatsStartYOff - 12;
        ctx.FillRect(L.box.x + 16, sepY, L.box.w - 32, 1, kColDivider);

        // --- Boutons "+1" par attribut primaire (seulement s'il reste des points) ---
        if (hasPoints) {
            for (int i = 0; i < kPrimaryAttrCount; ++i) {
                const Rect& r = L.plusBtn[i];
                const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
                const D3DCOLOR col = plusArmed_[i] ? kColBtnDown : (hover ? kColHover : kColBtn);
                ctx.FillRect(r.x, r.y, r.w, r.h, col);
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColFrame, 1);
            }
        }
        // --- Boutons "+5" (sprite unk_940260) : dessinés seulement à partir de 5
        // points non dépensés (cDrawWin_Draw 0x62A3D3) ---
        if (hasPoints5) {
            for (int i = 0; i < kPrimaryAttrCount; ++i) {
                const Rect& r = L.plus5Btn[i];
                const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
                const D3DCOLOR col = plus5Armed_[i] ? kColBtnDown : (hover ? kColHover : kColBtn);
                ctx.FillRect(r.x, r.y, r.w, r.h, col);
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColFrame, 1);
            }
        }
        return;
    }

    // --- Phase texte -----------------------------------------------------
    // Titre centré dans le bandeau.
    const int titleW = ctx.MeasureText("Personnage");
    ctx.Text("Personnage", L.box.x + (L.box.w - titleW) / 2, L.titleBar.y + 6, kColTitle);
    ctx.Text("X", L.closeBtn.x + 5, L.closeBtn.y + 2, kColText);

    // Ligne niveau + points non dépensés.
    std::snprintf(buf, sizeof(buf), "Niveau %d", self.level);
    ctx.Text(buf, L.box.x + 20, L.box.y + kTitleH + 12, kColText);
    std::snprintf(buf, sizeof(buf), "Points non dépensés : %d", self.unspentAttr);
    ctx.Text(buf, L.box.x + 180, L.box.y + kTitleH + 12,
             hasPoints ? kColUnspent : kColLabel);

    // Attributs primaires — grille RÉELLE 2 colonnes x 2 lignes (cf. bandeau
    // CONFIRME_FIDELE du .h : cDrawWin_Draw dessine les 4 valeurs à
    // (+107,+110)/(+203,+110)/(+107,+132)/(+203,+132) alignées à droite, bouton
    // "+1" juste à gauche de chaque colonne). Le libellé texte (absent du binaire
    // d'origine — probablement intégré au bitmap de fond) reste une adjonction
    // pragmatique, positionnée juste avant chaque valeur.
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        const auto attr = static_cast<PrimaryAttr>(i);
        const int col = i % 2;
        const int row = i / 2;
        const int y = L.box.y + (row == 0 ? kAttrRowY0 : kAttrRowY1);
        const int valueRightX = L.box.x + (col == 0 ? kAttrValueColX0 : kAttrValueColX1);

        std::snprintf(buf, sizeof(buf), "%d", AttrValue(self, attr));
        const int vw = ctx.MeasureText(buf);
        ctx.Text(buf, valueRightX - vw, y, kColText);

        std::snprintf(buf, sizeof(buf), "%s :", AttrLabel(attr));
        const int lw = ctx.MeasureText(buf);
        ctx.Text(buf, valueRightX - vw - lw - 4, y, kColLabel);

        if (hasPoints) {
            const Rect& r = L.plusBtn[i];
            const int plusW = ctx.MeasureText("+");
            ctx.Text("+", r.x + (r.w - plusW) / 2, r.y + 1, kColText);
        }
        if (hasPoints5) {
            const Rect& r = L.plus5Btn[i];
            const int plus5W = ctx.MeasureText("5");
            ctx.Text("5", r.x + (r.w - plus5W) / 2, r.y + 1, kColText);
        }
    }

    // Stats dérivées — 2 colonnes x 6 lignes (byte-exact, lues depuis SelfState,
    // calculées par StatEngine::Recompute via Game/StatFormulas.h/.cpp).
    struct StatRow { const char* label; int value; D3DCOLOR color; };
    const StatRow col1[6] = {
        { "Vie Max",            self.maxHp,       kColHp   },
        { "Mana Max",           self.maxMp,       kColMp   },
        { "Attaque Externe",    self.extAtk,      kColText },
        { "Attaque Interne",    self.intAtk,      kColText },
        { "Défense Externe",    self.extDef,      kColText },
        { "Défense Interne",    self.intDef,      kColText },
    };
    const StatRow col2[6] = {
        { "Précision",          self.accuracy,     kColText },
        { "Esquive",            self.evasion,      kColText },
        { "Taux Critique",      self.critRate,     kColText },
        { "Rating Att. Min",    self.atkRatingMin, kColText },
        { "Rating Att. Max",    self.atkRatingMax, kColText },
        { "Vitesse Attaque",    self.attackSpeed,  kColText },
    };

    const int col1LabelX = L.box.x + 20;
    const int col1ValueX = L.box.x + 210;
    const int col2LabelX = L.box.x + 250;
    const int col2ValueX = L.box.x + 445;

    for (int i = 0; i < 6; ++i) {
        const int y = L.box.y + kStatsStartYOff + i * kRowH;

        std::snprintf(buf, sizeof(buf), "%s :", col1[i].label);
        ctx.Text(buf, col1LabelX, y, kColLabel);
        std::snprintf(buf, sizeof(buf), "%d", col1[i].value);
        const int v1w = ctx.MeasureText(buf);
        ctx.Text(buf, col1ValueX - v1w, y, col1[i].color);

        std::snprintf(buf, sizeof(buf), "%s :", col2[i].label);
        ctx.Text(buf, col2LabelX, y, kColLabel);
        std::snprintf(buf, sizeof(buf), "%d", col2[i].value);
        const int v2w = ctx.MeasureText(buf);
        ctx.Text(buf, col2ValueX - v2w, y, col2[i].color);
    }
}

} // namespace ts2::ui

// UI/ShareBoxWindow.cpp — implémentation de UI_ShareBoxDlg (0x5CDDD0 / dword_1822560)
// = PANNEAU DE CONFIGURATION DE LA CEINTURE AUTO-POTION.
// Voir UI/ShareBoxWindow.h pour la sémantique réelle (le symbole IDA est trompeur),
// les corrections au dossier de gaps et le contrat de câblage.
//
// Aucun include Net/ : cette fenêtre n'émet RIEN aujourd'hui (Net_QueueAction16
// 0x512B90 n'existe pas côté C++, cf. MoveItem() plus bas) — donc pas de
// contrainte d'ordre <winsock2.h>/<windows.h> ici (même profil d'includes que
// UI/SocialWindow.cpp / UI/QuestTrackerWindow.cpp).
#include "UI/ShareBoxWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameDatabase.h"   // game::GetItemInfo / game::ItemInfo::iconId (+192)
#include "Game/GameState.h"      // game::g_World.self.mode (= g_SelfActionState 0x1687328)
#include "Net/ClientState.h"     // ts2::net::g_MorphInProgress (= g_MorphInProgress 0x1675A88)

#include <cstdio>

namespace ts2::ui {

namespace {

// Instance active (une seule dans tout le process, possédée par GameWindows).
// Sert les 3 points de câblage statiques documentés dans le .h.
ShareBoxWindow* g_activeShareBox = nullptr;

// Fond de panneau réel « best effort » — repli automatique sur kColPanelBg si
// absent (cf. méthodologie dans UI/PanelSkin.h). Le vrai fond est le sprite
// unk_977404 (EA 0x5CE5DF), dont le fichier .IMG n'est pas résolu statiquement.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00300.IMG");

// --- Adresses d'origine (longue traîne, stockée via game::g_Client.Var) -------
// RÉUTILISÉES telles quelles, PAS dupliquées : ce sont exactement les mêmes
// slots que ceux écrits par Net/ItemActionDispatch.cpp:98-99 (kAutoPotionBelt /
// kAutoPotionTimer), Net/GameVarDispatch.cpp:537-578 et
// Net/GameHandlers_InvCells.cpp:570-574.
constexpr uint32_t kAutoPotionBelt  = 0x16757B0; // g_AutoPotionBelt[i]  : itemID
constexpr uint32_t kAutoPotionTimer = 0x16757D8; // dword_16757D8[i]     : charges (/30)
constexpr uint32_t kInvSelSlot      = 0x1675800; // dword_1675800        : slot inventaire sélectionné
constexpr uint32_t kInvSelCount     = 0x1675804; // dword_1675804        : compteur associé
constexpr uint32_t kBeltSelSlot     = 0x16760E0; // dword_16760E0        : slot ceinture sélectionné
constexpr uint32_t kGateA           = 0x1687310; // dword_1687310[0]     : garde de MoveItem
constexpr uint32_t kGateB           = 0x1687474; // dword_1687474[0]     : garde de MoveItem
constexpr uint32_t kSysMsgColorAddr = 0x84DFD8;  // g_SysMsgColor

// --- Identifiants StrTable005 relevés dans UI_ShareBox_MoveItem 0x5CEAB0 ------
constexpr int kStrSlotOutOfRange = 2398; // EA 0x5CEAE5 / 0x5CEB60
constexpr int kStrSlotEmpty      = 2399; // EA 0x5CEB21 / 0x5CEB9C
constexpr int kStrBadActionState = 925;  // EA 0x5CEBCD
constexpr int kStrGateRefused    = 1186; // EA 0x5CEC0D

// g_SysMsgColor 0x84DFD8 — non modélisé en champ propre, longue traîne via Var()
// (même convention que UI/PartyWindow.cpp:47 / Game/SocialSystem.cpp:68).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
}

int32_t BeltItemId(int i)  { return game::g_Client.VarGet(kAutoPotionBelt  + 4u * static_cast<uint32_t>(i)); }
int32_t BeltCharges(int i) { return game::g_Client.VarGet(kAutoPotionTimer + 4u * static_cast<uint32_t>(i)); }

// Résolveur d'icône d'objet — IDENTIQUE à UI/WarehouseWindow.cpp:23 et
// UI/InventoryWindow.cpp (pattern de référence, dupliqué faute de header commun).
// Le binaire indexe `&g_AssetMgr_ItemIconSlots + 148 * (ITEM_INFO[+192] - 1)`
// (EA 0x5CE685/0x5CE6A2) : le slot 0-based de l'atlas = iconId - 1, ce qui
// correspond au fichier 002_%05u.IMG d'indice `iconId` (1-based) — même
// convention que les 4 autres fenêtres à icônes (cf. Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md).
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}

} // namespace

// ============================================================================
// Cycle de vie / instance active
// ============================================================================
ShareBoxWindow::ShareBoxWindow() {
    // Centrage par défaut (résolution de référence) — réellement recalculé à
    // chaque événement par RecomputeCenter(), comme le binaire.
    RecomputeCenter(ts2::kRefWidth, ts2::kRefHeight);
    g_activeShareBox = this;
}

ShareBoxWindow::~ShareBoxWindow() {
    if (g_activeShareBox == this) g_activeShareBox = nullptr;
}

ShareBoxWindow* ShareBoxWindow::Active() { return g_activeShareBox; }

// Miroir de l'EA 0x46AF6C : `call UI_ShareBoxDlg_Open` en queue du typeCode 26 de
// Pkt_ItemActionDispatch 0x46A320 (branche serveur, aujourd'hui ABANDONNÉE côté
// C++ — cf. Net/ItemActionDispatch.cpp::HandleAutoPotionBelt, TODO l.296).
void ShareBoxWindow::OpenActive() {
    if (g_activeShareBox) g_activeShareBox->Open();
}

// UI_ShareBoxDlg_Open 0x5CE0C0 : *(this+10) = 1 (EA 0x5CE0CC) puis boucle
// `for (i=0; i<4; ++i) *(this+i+11) = 0` (EA 0x5CE0D3).
// FIDÉLITÉ : la boucle remet 4 latches à zéro alors que seuls 2 sont utilisés
// (+11 = action, +13 = fermer ; +12 et +14 ne sont lus NULLE PART dans
// Draw/OnLDown/OnLUp). Remettre à zéro les deux latches modélisés est donc
// STRICTEMENT équivalent — les deux champs fantômes n'ont aucun lecteur.
void ShareBoxWindow::Open() {
    Dialog::Open();
    actionLatch_ = false;
    closeLatch_  = false;
}

// UI_ShareBoxDlg_Close 0x5CE100 : *(this+10) = 0, rien d'autre — aucune
// émission, aucun reset de sélection.
void ShareBoxWindow::Close() {
    Dialog::Close();
}

// ============================================================================
// Géométrie
// ============================================================================
// Draw 0x5CE567/0x5CE58F, OnLDown 0x5CE15D/0x5CE182, OnLUp 0x5CE36B/0x5CE390 :
//   x = nWidth/2  - Sprite2D_GetWidth(unk_977404)/2
//   y = nHeight/2 - Sprite2D_GetHeight(unk_977404)/2
// La largeur/hauteur du sprite de fond n'étant pas connaissable statiquement,
// on utilise l'étendue de repli kPanelW/kPanelH (cf. .h). Le CENTRAGE, lui, est
// exact et refait à chaque événement comme dans le binaire.
void ShareBoxWindow::RecomputeCenter(int screenW, int screenH) {
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

ShareBoxWindow::Rect ShareBoxWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

// EA 0x5CE629 / 0x5CE64A : (x + 55*(i%5) + 19, y + 55*(i/5) + 41).
ShareBoxWindow::Rect ShareBoxWindow::SlotRect(int i) const {
    return { x_ + kSlotPitch * (i % kSlotCols) + kSlotOx,
             y_ + kSlotPitch * (i / kSlotCols) + kSlotOy,
             kSlotPitch, kSlotPitch };
}

ShareBoxWindow::Rect ShareBoxWindow::ActionButtonRect() const {
    return { x_ + kBtnActionX, y_ + kBtnActionY, kBtnW, kBtnH };
}

ShareBoxWindow::Rect ShareBoxWindow::CloseButtonRect() const {
    return { x_ + kBtnCloseX, y_ + kBtnCloseY, kBtnW, kBtnH };
}

// EA 0x5CE209 — comparaisons STRICTES des deux côtés (bornes exclusives) :
//   a4 > x+55*(i%5)+19 && a4 < x+55*(i%5)+74 && a5 > y+55*(i/5)+41 && a5 < y+55*(i/5)+96
// (74 = 19+55, 96 = 41+55). Ce n'est PAS PointInRect : on écrit la comparaison
// à la main pour rester fidèle (un clic pile sur le bord ne compte pas).
bool ShareBoxWindow::SlotAt(int mx, int my, int& outSlot) const {
    for (int i = 0; i < kSlots; ++i) {
        if (BeltItemId(i) < 1) continue;                       // `>= 1` (EA 0x5CE209)
        const Rect r = SlotRect(i);
        if (mx > r.x && mx < r.x + kSlotPitch &&
            my > r.y && my < r.y + kSlotPitch) {
            outSlot = i;
            return true;
        }
    }
    return false;
}

// ============================================================================
// Événements souris
// ============================================================================
// UI_ShareBoxDlg_OnLDown 0x5CE120.
bool ShareBoxWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x5CE12D

    // Le binaire recentre AVANT le hit-test (EA 0x5CE144..0x5CE182). On ne
    // dispose pas des dimensions écran ici (Dialog::OnMouseDown ne reçoit pas
    // d'UiContext) : x_/y_ portent le centrage de la dernière frame rendue, ce
    // qui est équivalent tant que la résolution ne change pas entre deux frames
    // — même compromis que WarehouseWindow/MsgBoxDialog (cf. lastScreenW_ dans
    // UI/UIManager.h).
    int slot = -1;
    if (SlotAt(x, y, slot)) {
        // [audio] Snd3D_PlayScaledVolume(flt_1487E3C, .., 100, 1) (EA 0x5CE216).
        // Non porté : aucun registre d'émetteurs 3D par adresse n'existe côté
        // C++ (convention établie : UI/Widgets.cpp:60, UI/NpcDialogWindow.cpp:287,
        // UI/PartyWindow.cpp:307 commentent le son sans le porter).
        game::g_Client.Var(kBeltSelSlot) = slot;                // dword_16760E0 = i (EA 0x5CE21E)
        return true;                                            // EA 0x5CE229
    }

    const Rect act = ActionButtonRect();
    if (PointInRect(x, y, act.x, act.y, act.w, act.h)) {
        // [audio] flt_1487E3C (EA 0x5CE27B) — cf. ci-dessus.
        actionLatch_ = true;                                    // *(this+11)=1 (EA 0x5CE283)
        return true;
    }

    const Rect cls = CloseButtonRect();
    if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h)) {
        // [audio] flt_1487E3C (EA 0x5CE2DC) — cf. ci-dessus.
        closeLatch_ = true;                                     // *(this+13)=1 (EA 0x5CE2E4)
        return true;
    }

    // Repli : le binaire renvoie Sprite2D_HitTest(unk_977404, x, y, a4, a5)
    // (EA 0x5CE313) = « le clic est-il dans le fond ? ». La fenêtre consomme donc
    // le clic dès qu'il tombe sur le panneau, sans rien déclencher.
    const Rect panel = PanelRect();
    return PointInRect(x, y, panel.x, panel.y, panel.w, panel.h);
}

// UI_ShareBoxDlg_OnLUp 0x5CE330 — structure en if/else if EXCLUSIF (le binaire
// teste +11, SINON +13 ; jamais les deux), et chaque branche désarme son latch
// AVANT le hit-test, puis renvoie 1 même si le hit-test échoue (EA 0x5CE3E3 /
// 0x5CE44D). Reproduit tel quel.
bool ShareBoxWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x5CE33B

    if (actionLatch_) {
        actionLatch_ = false;                                   // EA 0x5CE39F
        const Rect act = ActionButtonRect();
        if (PointInRect(x, y, act.x, act.y, act.w, act.h))
            MoveItem(1, 1);                                     // `push 1; push 1` EA 0x5CE3EA-0x5CE3F1
        return true;                                            // EA 0x5CE3E3
    }

    if (closeLatch_) {
        closeLatch_ = false;                                    // EA 0x5CE409
        const Rect cls = CloseButtonRect();
        if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h))
            Close();                                            // UI_ShareBoxDlg_Close (EA 0x5CE457)
        return true;                                            // EA 0x5CE44D
    }

    return false;                                               // EA 0x5CE463
}

// ============================================================================
// UI_ShareBox_MoveItem 0x5CEAB0 — transcription intégrale
// ============================================================================
// `verbose` = a1 (gate des messages), `action` = a2 (code d'action).
//
// INATTEIGNABLE EN PRATIQUE : la branche `action != 1` (indexée par
// dword_1675800) n'est atteinte par AUCUN appelant — les deux seuls sites
// vivants passent (1,1) : EA 0x5CE3EA-0x5CE3EC (OnLUp, ci-dessus) et
// EA 0x679FE8-0x679FEA (UI_GameHud_ProcNet case 47). Elle est transcrite pour
// rester fidèle au CORPS de la fonction, pas parce qu'elle s'exécute.
//
// Noter deux asymétries RÉELLES du binaire, reproduites telles quelles :
//   - le message 1186 (kStrGateRefused) n'est PAS gaté par `verbose` (EA 0x5CEC02),
//     contrairement aux messages 2398/2399/925 ;
//   - la garde morph (`g_MorphInProgress == 1`) retourne SILENCIEUSEMENT, sans
//     aucun message (EA 0x5CEBE6).
void ShareBoxWindow::MoveItem(int verbose, int action) {
    const uint32_t beltSel = static_cast<uint32_t>(game::g_Client.VarGet(kBeltSelSlot));
    const uint32_t invSel  = static_cast<uint32_t>(game::g_Client.VarGet(kInvSelSlot));

    bool reachedCommonGates = false;

    if (action == 1) {                                          // EA 0x5CEABB
        if (beltSel >= static_cast<uint32_t>(kSlots)) {         // EA 0x5CEAC4 (comparaison NON SIGNÉE)
            if (verbose) game::g_Client.msg.System(game::Str(kStrSlotOutOfRange), SysMsgColor()); // EA 0x5CEAE5
            return;
        }
        if (BeltCharges(static_cast<int>(beltSel)) < 1) {       // EA 0x5CEB08
            if (verbose) game::g_Client.msg.System(game::Str(kStrSlotEmpty), SysMsgColor());      // EA 0x5CEB21
            return;
        }
        reachedCommonGates = true;                              // goto LABEL_19
    } else {
        if (invSel < static_cast<uint32_t>(kSlots)) {           // EA 0x5CEB3F
            if (BeltCharges(static_cast<int>(invSel)) >= 1) {   // EA 0x5CEB83
                reachedCommonGates = true;                      // LABEL_19
            } else {
                if (verbose) game::g_Client.msg.System(game::Str(kStrSlotEmpty), SysMsgColor());  // EA 0x5CEB9C
                return;
            }
        } else {
            if (verbose) game::g_Client.msg.System(game::Str(kStrSlotOutOfRange), SysMsgColor()); // EA 0x5CEB60
            return;
        }
    }

    if (!reachedCommonGates) return;

    // --- LABEL_19 : gardes communes ---------------------------------------
    // g_SelfActionState[0] 0x1687328 ≡ game::g_World.self.mode — équivalence
    // ÉTABLIE et écrite par Net/ItemActionDispatch.cpp:255-257
    // (« self.mode ≡ g_SelfActionState, lu par CombatResultApply »).
    if (game::g_World.self.mode != 1) {                         // EA 0x5CEBB5
        if (verbose) game::g_Client.msg.System(game::Str(kStrBadActionState), SysMsgColor());     // EA 0x5CEBCD
        return;
    }

    // g_MorphInProgress 0x1675A88 -> ts2::net::g_MorphInProgress (Net/ClientState.h:18),
    // réellement entretenu par Net/GameHandlers_Misc.cpp:248. Retour SILENCIEUX.
    if (net::g_MorphInProgress == 1) return;                    // EA 0x5CEBE6

    if (game::g_Client.VarGet(kGateA) && !game::g_Client.VarGet(kGateB)) { // EA 0x5CEBFA
        // NON gaté par `verbose` — fidèle (EA 0x5CEC02).
        game::g_Client.msg.System(game::Str(kStrGateRefused), SysMsgColor());                     // EA 0x5CEC0D
        return;
    }

    // --- Émission -----------------------------------------------------------
    // TODO [ancre 0x5CEC28] builder absent : Net_QueueAction16(&g_PlayerCmdController, action)
    //   (0x512B90). Layout prouvé par décompilation :
    //     - garde `*(g_PlayerCmdController + 51600) == 0`      (EA 0x512BA5)
    //     - garde `Char_IsAttackAction(g_LocalPlayerSheet)`     (0x558A50, EA 0x512BB3) — NON porté
    //     - memcpy(v5, g_SelfMoveStateBlock 0x1687324, 0x48)    (EA 0x512BCC)   — NON modélisé
    //     - bloc 72 o : [0]=0, [1]=16, [2]=0.0f, [3..5]=pos(v6,v7,v8), [6..8]=0.0f,
    //                   [9]=[10]=v9 (cap), [11]=0, [12]=-1, [13]=0, [14]=action,
    //                   [15..17]=0                              (EA 0x512BD4..0x512C3B)
    //     - Net_SendPacket_Op15(&g_AutoPlayMgr, bloc72)         (EA 0x512C5C)
    //     - puis *(ctrl+51600)=1, *(ctrl+51604)=g_GameTimeSec,
    //       et si g_SelfActionState in {2,32} -> =1 + g_SelfAnimFrame=0 (EA 0x512C67..0x512CAE)
    //   Net_SendPacket_Op15(NetClient&, const void* data72) EXISTE (Net/SendPackets.h:105),
    //   mais l'envelopper exige g_SelfMoveStateBlock + Char_IsAttackAction +
    //   g_PlayerCmdController : c'est le périmètre de math-01 / vague W8 (backlog
    //   réseau), hors de ce front. AUCUN appel inventé ici (règle #8).
}

// ============================================================================
// Icônes
// ============================================================================
gfx::GpuTexture* ShareBoxWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t itemId) {
    const std::string path = ResolveItemIconPath(itemId);
    return ActiveIconCache().GetOrLoad(dev, path);
}

// ============================================================================
// UI_ShareBoxDlg_Draw 0x5CE4D0
// ============================================================================
void ShareBoxWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Recentrage à chaque frame (EA 0x5CE54B..0x5CE58F) — avant tout usage de x_/y_.
    RecomputeCenter(ctx.screenW, ctx.screenH);
    if (!bOpen_) return;                                        // EA 0x5CE4F0

    const Rect panel = PanelRect();
    const Rect act   = ActionButtonRect();
    const Rect cls   = CloseButtonRect();

    const int32_t invSelSlot  = game::g_Client.VarGet(kInvSelSlot);   // dword_1675800
    const int32_t invSelCount = game::g_Client.VarGet(kInvSelCount);  // dword_1675804
    const int32_t beltSelSlot = game::g_Client.VarGet(kBeltSelSlot);  // dword_16760E0

    if (ctx.phase == UiPhase::Panels) {
        // Sprite2D_HitTest(unk_977404, ...) -> Util_SetClampedU8Field(dword_8E714C, 0)
        // (EA 0x5CE5B2/0x5CE5C2) : remise du curseur au slot 0 au survol du panneau.
        // NON porté : CursorSet::SetActiveSlot n'a aucun appelant dans tout l'arbre et
        // `cursors_` est un membre PRIVÉ d'App (App/App.h:43) — c'est le gap UTIL-01,
        // dont le correctif (exposer l'instance) est explicitement hors de ce front.
        // TODO [ancre 0x5CE5C2] : Cursors().SetActiveSlot(0) au survol, une fois UTIL-01 câblé.

        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColPanelBg); // Sprite2D_Draw(unk_977404) EA 0x5CE5DF
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;

        // Boucle des 10 emplacements (EA 0x5CE5E4).
        for (int i = 0; i < kSlots; ++i) {
            const int32_t itemId = BeltItemId(i);
            const Rect r = SlotRect(i);

            // Emplacement vide -> le binaire ne dessine RIEN (garde `>= 1`, EA 0x5CE60B) :
            // pas de case, pas de cadre, pas de teinte — l'art du sprite de fond fait foi.
            // On s'en tient STRICTEMENT à cela (règle de fidélité) : ne pas ajouter de
            // case sombre « pour voir la grille », ce serait une invention visuelle.
            if (itemId < 1) continue;

            // MobDb_GetEntry(mITEM, belt[i]) (EA 0x5CE662) : si l'entrée est
            // introuvable, le binaire ne dessine RIEN (garde `if (Entry)` EA 0x5CE66F).
            const game::ItemInfo* info = game::GetItemInfo(static_cast<uint32_t>(itemId));
            if (!info) continue;

            gfx::GpuTexture* icon = GetIconTex(dev, static_cast<uint32_t>(itemId));
            if (icon && icon->Handle() && ctx.sprites) {
                // Sprite2D_Draw(&g_AssetMgr_ItemIconSlots + 148*(iconId-1), v21, v26)
                // (EA 0x5CE6A2) : blit à la taille NATIVE du sprite, sans mise à
                // l'échelle -> DrawSprite (et non DrawSpriteScaled).
                ctx.sprites->DrawSprite(icon->Handle(), nullptr, r.x, r.y, gfx::kSpriteWhite);
            } else {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColSlotBg);   // repli si l'icône ne charge pas
            }

            // Surbrillance unk_94D970 : `dword_1675800 == i && dword_1675804 > 0`
            // (EA 0x5CE6B8) — sprite d'overlay dans le binaire, cadre coloré ici.
            if (invSelSlot == i && invSelCount > 0)
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColSelInv, 2);   // EA 0x5CE6CA

            // Surbrillance unk_947A0C : `dword_16760E0 == i` (EA 0x5CE6D7).
            if (beltSelSlot == i)
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColSelBelt, 2);  // EA 0x5CE6E9
        }

        // --- Boutons -------------------------------------------------------
        // FIDÉLITÉ : le binaire ne dessine un bouton QUE s'il est pressé (sprite
        // « down ») ou survolé (sprite « up ») — l'état au repos n'est PAS dessiné,
        // car son visuel appartient au sprite de FOND. On reproduit cette logique
        // (rien au repos) tout en gardant un rectangle de repli visible quand le
        // fond .IMG n'a pas pu être chargé... ce qu'on ne peut pas distinguer ici :
        // on dessine donc le bouton au repos en teinte neutre, écart ASSUMÉ et
        // documenté (sans lui, les boutons seraient invisibles avec le fond de repli).
        const bool actHover = PointInRect(cursorX, cursorY, act.x, act.y, act.w, act.h);
        ctx.FillRect(act.x, act.y, act.w, act.h,
                     actionLatch_ ? kColBtnDown : (actHover ? kColBtnHover : kColBtnBg)); // EA 0x5CE7C3 / 0x5CE784
        ctx.DrawFrame(act.x, act.y, act.w, act.h, kColFrame, 1);

        const bool clsHover = PointInRect(cursorX, cursorY, cls.x, cls.y, cls.w, cls.h);
        ctx.FillRect(cls.x, cls.y, cls.w, cls.h,
                     closeLatch_ ? kColBtnDown : (clsHover ? kColBtnHover : kColBtnBg));  // EA 0x5CE898 / 0x5CE859
        ctx.DrawFrame(cls.x, cls.y, cls.w, cls.h, kColFrame, 1);
        return;
    }

    // --- Phase texte --------------------------------------------------------
    ctx.Text("Ceinture", panel.x + 8, panel.y + 6, kColTitle);

    // Seconde boucle des 10 emplacements (EA 0x5CE89D) — noter que sa garde est
    // `if (g_AutoPotionBelt[i])` (!= 0, EA 0x5CE8BC), et NON `>= 1` comme la
    // boucle d'icônes : un itemID négatif afficherait donc son compteur sans
    // icône. Reproduit tel quel.
    for (int i = 0; i < kSlots; ++i) {
        if (BeltItemId(i) == 0) continue;                       // EA 0x5CE8BC

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d / %d",              // aD D « %d / %d » (EA 0x5CE8E1)
                      static_cast<int>(BeltCharges(i)), kMaxCharges);

        // v22 = x + 55*(i%5) + 44 - UI_MeasureNumberText(buf)/2  (EA 0x5CE8FF..0x5CE91E)
        // v26 = y + 55*(i/5) + 77                                 (EA 0x5CE93F)
        // UI_DrawNumberValue(buf, v22, v26, 1) — couleur 1 (EA 0x5CE95B).
        const int cx = x_ + kSlotPitch * (i % kSlotCols) + kCountDx;
        const int cy = y_ + kSlotPitch * (i / kSlotCols) + kCountDy;
        ctx.Text(buf, cx - ctx.MeasureText(buf) / 2, cy, kColText);
    }

    // Libellés de repli des deux boutons (le binaire n'écrit AUCUN texte : les
    // libellés font partie des sprites unk_977498 / unk_9776E8). Écart assumé,
    // nécessaire tant que les sprites ne sont pas résolus.
    const char* actLbl = "Assigner";
    ctx.Text(actLbl, act.x + (act.w - ctx.MeasureText(actLbl)) / 2, act.y + (act.h - 12) / 2, kColText);
    const char* clsLbl = "Fermer";
    ctx.Text(clsLbl, cls.x + (cls.w - ctx.MeasureText(clsLbl)) / 2, cls.y + (cls.h - 12) / 2, kColText);
}

} // namespace ts2::ui

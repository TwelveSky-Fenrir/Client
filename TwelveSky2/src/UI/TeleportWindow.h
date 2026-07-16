// UI/TeleportWindow.h — PAGE 76 de la méga-fenêtre `cNpcWin` : téléportation payante
// vers l'une des 4 destinations de la faction (service code 76 / 0x4C du menu PNJ).
//
// ANCRAGE (Passe 4 / vague W11, front `w11-npc-vendor-warp`) — SEPT fonctions du binaire :
//   cTeleportWin_Init            0x627BA0  init page 76 (*(this+180)=76, purge 100 verrous)
//   cTeleportWin_OnMouseDown     0x627BF0  hit-test des 4 lignes + bouton fermer, arme verrou
//   cTeleportWin_OnCommit        0x627D50  relâchement : gardes niveau/var + arme+émet Op20
//   cTeleportWin_Draw            0x628030  rendu de la liste (3 états de couleur par ligne)
//   cTeleportWin_GetSlotCount    0x628250  = return 4      (nombre de destinations)
//   cTeleportWin_GetDestMapId    0x6282D0  slot 0..3 -> mapId {313,316,331,334}, défaut 0
//   cTeleportWin_FormatEntryLabel 0x628260 "zoneName suffix" (StrTable003 + StrTable005[225])
//
// VIVACITÉ (chaîne d'appel prouvée dans l'IDB, xref unique chacune) :
//   INIT   : App_WndProc 0x461930 -> Input_OnLButtonUp 0x50AD20 -> UI_RouteLButtonUp 0x5AD0F0
//            -> UI_NpcWin_OnLUp_Dispatch 0x5DD3B0 -> UI_NpcMenu_OnLUp 0x5DF640
//            @0x5dfad4 (jumptable 005DF72C case 76) -> cTeleportWin_Init 0x627BA0.
//   DOWN   : ... UI_NpcWin_OnLDown_Dispatch 0x5DCB10 @0x5dd24a -> cTeleportWin_OnMouseDown.
//   COMMIT : ... UI_NpcWin_OnLUp_Dispatch 0x5DD3B0 @0x5ddaea (case 76) -> cTeleportWin_OnCommit.
//   DRAW   : Scene_*Render -> UI_RenderAllDialogs 0x5AE2D0 -> UI_NpcWin_Draw_Dispatch 0x5DE180
//            @0x5de765 -> cTeleportWin_Draw 0x628030.
//
// MODÈLE OBJET : dans le binaire, page 0 (menu, UI/NpcDialogWindow) et page 76 (cette classe)
// sont le MÊME objet cNpcWin ; le champ *(this+180) (id de page) fait basculer les dispatchers
// OnLDown/OnLUp/Draw d'un jeu de handlers à l'autre. Les deux pages sont donc MUTUELLEMENT
// EXCLUSIVES (jamais dessinées en même temps). Côté C++ on les modélise en DEUX classes Dialog
// distinctes : NpcDialogWindow::DispatchService(76) OUVRE cette fenêtre et FERME le menu, ce
// qui reproduit fidèlement le remplacement de page (cf. NpcDialogWindow::OpenTeleportPage).
//
// ⚠️ CÂBLAGE (hors de mes fichiers, signalé à l'orchestrateur) :
//   1. UI/GameWindows.h/.cpp doivent POSSÉDER une instance `teleport_`, l'ENREGISTRER dans
//      UIManager (sans quoi Render/OnMouseDown/OnClick ne sont jamais routés), et appeler
//      `npcDialog_.SetTeleportWindow(&teleport_)` (sans quoi le service code 76 n'ouvre rien).
//   2. Défaut AMONT préexistant, NON introduit ici : NpcDialogWindow::Open() n'est appelé
//      nulle part (le routage clic-PNJ monde 3D n'est pas porté). Le menu PNJ — donc la
//      destination 76 — reste inatteignable tant que ce front n'existe pas. TeleportWindow
//      HÉRITE de ce blocage, elle ne l'aggrave pas.
#pragma once
#include "UI/UIManager.h"

namespace ts2::ui {

// Page 76 « téléportation payante » de cNpcWin. N'A que 4 destinations fixes (GetSlotCount
// 0x628250 = 4) mappées sur des mapIds constants (GetDestMapId 0x6282D0).
class TeleportWindow : public Dialog {
public:
    TeleportWindow();

    // Nombre de destinations (cTeleportWin_GetSlotCount 0x628250 @0x62825c : return 4).
    static constexpr int kSlotCount = 4;

    // cTeleportWin_Init 0x627BA0 : *(this+180)=76 puis purge des 100 verrous *(this+70..169).
    // Seuls les 5 premiers (fermer + 4 lignes) sont utilisés par cette page.
    void Open() override;
    void Close() override;

    // cTeleportWin_OnMouseDown 0x627BF0 (via UI_NpcWin_OnLDown_Dispatch page 76).
    bool OnMouseDown(int x, int y) override;
    // cTeleportWin_OnCommit 0x627D50 (via UI_NpcWin_OnLUp_Dispatch page 76).
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    // cTeleportWin_Draw 0x628030 (via UI_NpcWin_Draw_Dispatch page 76).
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Introspection (tests / front de câblage). `slot` dans [0, kSlotCount).
    // cTeleportWin_GetDestMapId 0x6282D0.
    static int32_t DestMapId(int slot);

private:
    struct Rect { int x, y, w, h; };

    // Recentrage à CHAQUE appel, comme le binaire (nWidth/2 - Sprite2D_GetWidth(&unk_8FE3E0)/2,
    // nHeight/2 - Sprite2D_GetHeight(&unk_8FE3E0)/2 — 0x627c04/0x627d64/0x628054).
    void Recenter(int screenW, int screenH);

    // Rectangle de la ligne `i` — INÉGALITÉS STRICTES du binaire (mx>x+37 && mx<x+217 &&
    // my>y+18i+26 && my<y+18i+38), height 12, pitch 18. EA identiques dans OnMouseDown
    // (0x627cXX), OnCommit (0x627e4c..0x627e85) et Draw hover (0x62818f). NB : ce n'est PAS
    // Dialog::PointInRect (qui borne à gauche par >=) — d'où le test explicite.
    bool RowHit(int i, int mx, int my) const;
    // Bouton fermer : Sprite2D_HitTest(&unk_8F3798, x+235, y+4, mx, my) — 0x627c68/0x627de0.
    bool CloseButtonHit(int mx, int my) const;

    // « Arme le bloc de warp (mode 6) puis émet Op20 » — transcription LOCALE de la queue de
    // cTeleportWin_OnCommit 0x627f65..0x628008 (le binaire n'a pas de fonction partagée ; même
    // choix que UI/NpcDialogWindow.cpp::ArmWarpAndSendOp20, corps identique à mode 6 près).
    void ArmWarpAndSendOp20(int32_t zoneId, const float pos[3]);

    // Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id), g_SysMsgColor).
    static void SysMsg(int strId);

    bool closeLatch_        = false;             // this+70    (armé 0x627c84, lu/purgé 0x627da8/db4)
    bool slotLatch_[kSlotCount] = {};            // this+71..74 (armé 0x627d29, lu/purgé 0x627e24/e36)

    // Dims écran du dernier Render : le hit-test (OnMouseDown/OnClick) est routé entre deux
    // frames et doit s'aligner sur la géométrie dessinée (même motif que MsgBoxDialog /
    // NpcDialogWindow — le binaire recentre dans OnLDown/OnLUp/Draw indifféremment).
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // TODO [ancre unk_8FE3E0 (panneau) / unk_8F3798 (bouton fermer)] : dimensions RÉELLES =
    // Sprite2D_GetWidth/Height des sprites d'atlas UI, non résolus côté ClientSource. Ces
    // placeholders ne servent QUE au fond/cadre et au recentrage ; les hit-tests utilisent
    // les offsets EXACTS du binaire (+37/+217/+18i+26/+18i+38, +235/+4), indépendants d'eux.
    static constexpr int kPanelW  = 264;   // >= 235 (bouton fermer) + marge
    static constexpr int kPanelH  = 112;   // 26 + 4*18 + marge basse
    static constexpr int kCloseW  = 20;    // placeholder bouton fermer
    static constexpr int kCloseH  = 20;

    static constexpr D3DCOLOR kColBg      = 0xE0202028u; // fond panneau (repli PanelSkin)
    static constexpr D3DCOLOR kColBorder  = 0xFF808080u; // cadre
    static constexpr D3DCOLOR kColRest    = 0xFFFFFFFFu; // état 1 (repos)
    static constexpr D3DCOLOR kColHover   = 0xFFFFDD66u; // état 3 (survol)
    static constexpr D3DCOLOR kColPressed = 0xFF66AAFFu; // état 2 (verrouillé/enfoncé)
    static constexpr D3DCOLOR kColClose   = 0xFFCC4040u; // bouton fermer (enfoncé)
};

} // namespace ts2::ui

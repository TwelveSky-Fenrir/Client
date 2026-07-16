// UI/NpcDialogWindow.h — menu de services PNJ, PAGE 0 de la méga-fenêtre `cNpcWin` (ts2::ui).
//
// ANCRAGE (Passe 4 / vague W6, front `quest-npcdialog`) — CE FICHIER A ÉTÉ RÉ-ANCRÉ :
// la version précédente modélisait une boîte « Accepter / Parler / Fermer » qui N'EXISTE PAS
// dans le binaire, et adossait « Accepter » à `Npc_Interact 0x53A660` qui est en réalité le
// RAMASSAGE DE LOOT (op201), pas le dialogue PNJ. Les deux ont été supprimés.
//
// L'objet réel est une méga-fenêtre à 78 pages (`this+180` = id de page) routée par :
//   UI_NpcWin_Open           0x5DB530   construction du menu + état
//   UI_NpcWin_CloseRestore   0x5DC1F0   fermeture (restaure les matériaux vers l'inventaire)
//   UI_NpcWin_OnLDown_Dispatch 0x5DCB10 -> page 0 : UI_NpcMenu_OnLDown 0x5DF560
//   UI_NpcWin_OnLUp_Dispatch   0x5DD3B0 -> page 0 : UI_NpcMenu_OnLUp   0x5DF640
//   UI_NpcWin_Draw_Dispatch    0x5DE180 -> page 0 : UI_NpcMenu_Draw    0x5DFC30
//   UI_NpcWin_OnKey            0x5DE030
// CETTE CLASSE NE PORTE QUE LA PAGE 0 (le menu de services). Les 77 autres pages (Shop,
// Warehouse, Craft, Enchant, …) sont d'autres fenêtres/fronts et NE SONT PAS ici : les codes
// de service qui y mènent laissent un TODO de délégation dans DispatchService().
//
// ---------------------------------------------------------------------------------------
// `this+2` EST UN `NpcDefRecord*` (table "mNPC" 005_00005.IMG, Game/ExtraDatabases.h) — PROUVÉ :
//   - UI_NpcWin_Open 0x5DB530 @0x5db54d : `*(this+2) = *(DWORD*)a2` où
//     a2 = &g_NpcRenderArray[22*idx] (0x1764D14, stride 88) — cf. Npc_ApproachAndInteract
//     0x539DC0 @0x539eb4 : `UI_NpcWin_Open(dword_1822EC8, (float*)&g_NpcRenderArray[22*a1])`.
//     Donc `this+2` = champ 0 de l'enregistrement de rendu PNJ = le pointeur de record mNPC.
//   - UI_NpcMenu_Draw 0x5DFC30 @0x5dfcea : `Crt_Vsnprintf(v7, "%s.....", *(this+2) + 4)`
//     -> `def+4` == NpcDefRecord::name. CONFIRMATION DIRECTE du type.
//   - @0x5dfd97 : ligne de salutation lue à `*(this+2) + 36 + 255*(*(this+186)) + 51*i`
//     -> `def+36 + 255*page + 51*ligne` == NpcDefRecord::textGrid[page][ligne]
//     (textGrid @+36, page = 5*51 = 255 o, ligne = 51 o). PROUVE que l'index de salutation
//     sélectionne une PAGE de textGrid — l'ancienne version prenait textGrid[0] arbitrairement.
//
// Ces trois offsets RÉSOLVENT trois champs jusqu'ici « rôle inconnu » de NpcDefRecord :
//   +32   fieldA      = nombre de pages de salutation actives (init de `greetingUsed_`,
//                        0x5dbffc : `this[ii+181] = (ii >= *(DWORD*)(def+32))`) — confirme
//                        l'hypothèse « nSpeechNum » de ExtraDatabases.h.
//   +1312 fieldB      = faction/tribu du PNJ (garde `def->fieldB - 2 == g_LocalElement`,
//                        0x5e19fe / 0x608b4e) — confirme l'hypothèse « nTribe ».
//   +1340 fieldG[100] = drapeaux de menu ; un service est proposé quand la valeur vaut 2
//                        (0x5db6b5 : `*(DWORD*)(def + 4*n + 1340) == 2`) — confirme « nMenu[100] ».
// (Ces champs restent nommés fieldA/fieldB/fieldG : ExtraDatabases.h ne m'appartient pas.)
//
// ---------------------------------------------------------------------------------------
// PÉRIMÈTRE DE DONNÉES : cette fenêtre ne reçoit QUE le `NpcDefRecord*` — c'est exactement
// ce que le binaire conserve dans `this+2`, et tout ce dont la page 0 a besoin (services
// +1340, salutations +32/+36, faction +1312). Le reste de `UI_NpcWin_Open` (queue de la
// fonction, 0x5dc01d..0x5dc0a8 : `a2[3]=1`, `a2[4]=0.0`, orientation du PNJ vers le joueur
// via Math_AngleBetween2D, Fx_MeleeSwingUpdate) mute l'ENTITÉ DE RENDU, pas la fenêtre :
// cela appartient au front qui possède le picking 3D / g_NpcRenderArray, pas à ce fichier.
// TODO [ancre UI_NpcWin_Open 0x5DB530 @0x5dc01d-0x5dc0a8] : queue « orientation + FX » à
// porter par le front picking 3D lorsqu'il appellera Open().
//
// ⚠️ CÂBLAGE MANQUANT (hors de mes fichiers, bloquant — signalé à l'orchestrateur) :
// `NpcDialogWindow::Open()` n'est appelé NULLE PART. Tant que le front qui possède le
// routage de clic monde 3D n'appelle pas `windows.NpcDialog().Open(def)` depuis l'équivalent
// de Npc_ApproachAndInteract 0x539DC0 (dist<=20) / Item_PickupTarget 0x539EC0, toute cette
// fenêtre reste inatteignable. Le binaire, lui, l'ouvre inconditionnellement.
#pragma once
#include "UI/UIManager.h"
#include "Game/ExtraDatabases.h"   // game::NpcDefRecord (table "mNPC")

namespace ts2::ui {

class NpcDialogWindow : public Dialog {
public:
    NpcDialogWindow();

    // Nombre de lignes de service du menu (boucles `i < 10` de UI_NpcWin_Open 0x5db5c1,
    // UI_NpcMenu_OnLDown 0x5df5b5, UI_NpcMenu_OnLUp 0x5df695, UI_NpcMenu_Draw 0x5dfdbb).
    static constexpr int kMaxServices = 10;
    // Nombre de pages de salutation (boucles `ii < 5` de UI_NpcWin_Open 0x5dbfbd et
    // UI_NpcMenu_PickGreeting 0x5dff1a) == première dimension de NpcDefRecord::textGrid.
    static constexpr int kGreetingSlots = 5;

    // UI_NpcWin_Open 0x5DB530. `npcDef` == `this+2` (record mNQC résolu par l'appelant via
    // game::GetNpcDefRecord(kindId), comme Npc_DrawMesh 0x57FF00 le fait pour g_NpcRenderArray).
    // nullptr toléré (aucun service, aucune salutation) : le binaire déréférence sans test,
    // mais un record introuvable ne peut pas arriver côté C++ sans crasher — repli explicite.
    void Open(const game::NpcDefRecord* npcDef);
    void Open() override { Dialog::Open(); } // ouverture nue (défaut du contrat Dialog)
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Introspection (tests / front de câblage). `row` dans [0, kMaxServices).
    int  ServiceCode(int row) const {
        return (row >= 0 && row < kMaxServices) ? serviceCodes_[row] : 0;
    }
    // -1 = aucune salutation encore tirée (état initial 0x5dc00c) ; 5 = toutes consommées
    // (saturation 0x5dff4f) ; 0..4 = page de NpcDefRecord::textGrid affichée.
    int  GreetingIndex() const { return greetingIdx_; }

private:
    struct Rect { int x, y, w, h; };

    // Géométrie recentrée à CHAQUE appel comme le binaire (nWidth/2 - spriteW/2,
    // nHeight/2 - spriteH/2 — UI_NpcMenu_OnLDown 0x5df574, OnLUp 0x5df654, Draw 0x5dfc54).
    void   Recenter(int screenW, int screenH);
    Rect   PanelRect() const { return { x_, y_, kPanelW, kPanelH }; }
    // Ligne de service `row` : Sprite2D_HitTest(&unk_8F7730, *this+12, *(this+1)+22*row+7, ...)
    // — UI_NpcMenu_OnLDown 0x5df606, OnLUp 0x5df6fb, Draw 0x5dfe2c. Mêmes 3 EA, même formule.
    Rect   ServiceRowRect(int row) const;

    // switch(*(this+i+170)) de UI_NpcMenu_OnLUp 0x5DF640 @0x5df72c.
    void   DispatchService(int code);
    // UI_NpcMenu_PickGreeting 0x5DFF00 (code 1) — purement local, n'émet rien.
    void   PickGreeting();

    // --- Handlers de service portés (un par case du switch, EA en tête d'implémentation) ---
    void   CastReturn();            // code 4    UI_NpcMenu_CastReturn                0x5E19E0 -> Op20
    void   WarpFactionTown();       // code 0x31 UI_WarpFactionTown                   0x608D40 -> Op20
    void   ClassChangeValidate();   // code 0x35 UI_ClassChange_Validate              0x60A310 -> MsgBox(46)
    void   SendOp116AndClose();     // code 0x37 UI_NpcMenu_OnLUp_SendOp116AndClose   0x60FA60 -> Op116
    void   FactionAdvanceCommit();  // code 0x3E UI_FactionAdvance_Commit             0x612C20 -> Op126

    // Garde partagée « le PNJ est de ma faction » : `*(DWORD*)(def+1312) - 2 == g_LocalElement`,
    // sinon Msg_AppendSystemLine(StrTable005_Get(143)) et AUCUNE émission.
    // EA : UI_NpcMenu_CastReturn 0x5e19fe, UI_ClanWarp_Commit 0x608b4e.
    bool   CheckNpcFaction();
    // Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id), g_SysMsgColor).
    static void SysMsg(int strId);

    const game::NpcDefRecord* npcDef_ = nullptr;      // this+2   (0x5db54d)
    int32_t serviceCodes_[kMaxServices] = {};          // this+170..179 (0x5db652/66c/fa6)
    bool    pressLatch_[kMaxServices]   = {};          // this+70..79   (armé 0x5df627, lu/purgé 0x5df6b7/6c9)
    bool    greetingUsed_[kGreetingSlots] = {};        // this+181..185 (0x5dbffc)
    int32_t greetingIdx_ = -1;                         // this+186      (0x5dc00c)

    // Dims écran du dernier Render : le hit-test (OnMouseDown/OnClick) est routé entre deux
    // frames et doit s'aligner sur la géométrie dessinée (même motif que MsgBoxDialog).
    // Le binaire recentre dans OnLDown/OnLUp/Draw indifféremment, d'où le même recentrage ici.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // TODO [ancre UI_NpcMenu_Draw 0x5DFC30 @0x5dfc70/0x5dfc98] dimensions RÉELLES du panneau =
    // Sprite2D_GetWidth/Height(&unk_8F7608) — sprite d'atlas UI non résolu côté ClientSource.
    // Placeholders dimensionnés pour contenir la grille PROUVÉE de 10 lignes (pitch 22, y+7).
    static constexpr int kPanelW = 240;
    static constexpr int kPanelH = 236;   // 7 + 10*22 + marge basse

    // Grille de lignes de service — valeurs PROUVÉES (0x5df606 / 0x5df6fb / 0x5dfe2c).
    static constexpr int kRowOffsetX = 12;
    static constexpr int kRowOffsetY = 7;
    static constexpr int kRowPitchY  = 22;
    // TODO [ancre 0x5DF6FB] largeur/hauteur réelles de la ligne = dims du sprite unk_8F7730
    // (Sprite2D_HitTest teste le rectangle du sprite) — non résolu, placeholders cohérents
    // avec le pitch prouvé.
    static constexpr int kRowW = kPanelW - 2 * kRowOffsetX;
    static constexpr int kRowH = 20;

    // Bandeau « nom + salutation » — PROUVÉ (UI_NpcMenu_Draw 0x5DFC30) : dessiné au-DESSUS du
    // panneau (offsets négatifs), et seulement si greetingIdx_ != -1 (0x5dfca8).
    static constexpr int kNameOffsetX     = 22;    // *this+22            (0x5dfd1c)
    static constexpr int kNameOffsetY     = -144;  // *(this+1)-144       (0x5dfd1c)
    static constexpr int kGreetOffsetY    = -121;  // *(this+1)+20*i-121  (0x5dfd97)
    static constexpr int kGreetPitchY     = 20;    // 20*i                (0x5dfd97)

    static constexpr D3DCOLOR kColBg       = 0xE0202028u; // fond panneau
    static constexpr D3DCOLOR kColBorder   = 0xFF808080u; // cadre
    static constexpr D3DCOLOR kColTitle    = 0xFFFFDD66u; // nom du PNJ (style 3 du binaire)
    static constexpr D3DCOLOR kColText     = 0xFFFFFFFFu; // ligne de salutation (style 1)
    static constexpr D3DCOLOR kColTextDim  = 0xFFAAAAAAu; // service non porté (TODO de délégation)
    static constexpr D3DCOLOR kColHover    = 0xFF4060A0u; // survol   (état +1 de l'atlas)
    static constexpr D3DCOLOR kColPressed  = 0xFF20304Cu; // enfoncé  (état +2 de l'atlas)
    static constexpr D3DCOLOR kColRowBg    = 0xFF3A3A46u; // ligne au repos (état +0)
};

} // namespace ts2::ui

// UI/PartyWindow.h — fenêtre « Groupe » (ts2::ui).
//
// ===========================================================================
// CORRECTION D'AUDIT (Passe 4, vague W6, 2026-07-16) — LIRE EN PREMIER
// ===========================================================================
// Le bandeau précédent de ce fichier concluait :
//   « AUCUNE fonction UI_PartyWin_*/UI_PartyHud_* n'existe dans l'IDB (contrairement
//     à UI_ClanWin_* pour la Guilde) : il n'y a PAS de fenêtre dédiée "Groupe". »
// La PRÉMISSE est exacte, la CONCLUSION était FAUSSE : la recherche portait sur le
// mauvais préfixe. La fenêtre de groupe du binaire EXISTE et s'appelle
// UI_MemberSelectWnd (objet global g_MemberSelectWnd 0x184BE38) :
//   UI_MemberSelectWnd_Open        0x667260  (garde arène, reset, -> Net_SendOp57)
//   UI_MemberSelectWnd_Close       0x667350  (*(this+2) = 0)
//   UI_MemberSelectWnd_OnMouseDown 0x667370  (hit-test 10 slots + 2 boutons)
//   UI_MemberSelectWnd_OnClick     0x667580  (Fermer / Valider -> UI_MsgBox 21)
//   UI_MemberSelectWnd_ProcNet     0x667730  (code file UI 33 = bascule ouvrir/fermer)
//   UI_MemberSelectWnd_Render      0x667860  (centrage écran nWidth/2 - w/2)
// Elle ÉMET DEUX PAQUETS (Op57 = 0x39 à l'ouverture, Op58 = 0x3A à la validation) :
// le « zéro émission réseau » de ce fichier était donc un VRAI défaut, désormais
// corrigé (cf. .cpp).
//
// ---------------------------------------------------------------------------
// dword_184BE40 N'EST PAS UN « FLAG GROUPE ACTIF »
// ---------------------------------------------------------------------------
// Le bandeau précédent (et Net/GameHandlers_PartyGuild.cpp:186) documentent
// dword_184BE40 comme « flag groupe actif ». C'est FAUX : c'est le champ +8 (bOpen)
// de l'objet fenêtre g_MemberSelectWnd 0x184BE38. Preuve par layout — 0x67814E /
// 0x67815A (UI_GameHud_OnClick) chargent `ecx, offset g_MemberSelectWnd` = 0x184BE38
// puis appellent _Open/_Close ; les trois globals réputés « dispersés » sont en
// réalité les champs d'un seul objet :
//   0x184BE38  +0   x                  (0x6673AD : *this = nWidth/2 - w/2)
//   0x184BE3C  +4   y                  (0x6673D2 : nHeight/2 - h/2)
//   0x184BE40  +8   bOpen              (0x66729F : *(this+2)=1 ; garde 0x66737D)
//   0x184BE44  +12  latch « Fermer »   (*(this+3), armé 0x6674EE)
//   0x184BE48  +16  latch « Valider »  (*(this+4), armé 0x66753D)
//   0x184BE4C  +20  slot sélectionné   (0x6672D1 : *(this+5) = -1)
//   0x184BE50  +24  valeurs[10]        (0x6672F6 : *(this+j+6) = -2)
// Vérification arithmétique : 0x184BE38+8 = 0x184BE40, +20 = 0x184BE4C, +24 = 0x184BE50.
//
// CONSÉQUENCE (chaîne de code mort rompue par cette vague) : PERSONNE n'écrivait
// jamais Var(0x184BE40) — il n'était que LU, par ce fichier (garde de visibilité) et
// par Net/GameHandlers_PartyGuild.cpp:186 (garde du handler 0x3f). VarGet renvoie 0
// pour une clé absente (Game/ClientRuntime.h:164) : les deux gardes étaient donc
// TOUJOURS fausses, ce qui rendait mort (a) tout ce panneau, (b) le corps du handler
// 0x3f, donc (c) le Net_SendOp57 de pagination du roster. Open()/Close() ci-dessous
// écrivent enfin ce flag (ancres 0x66729F / 0x667365), ce qui ressuscite la chaîne.
//
// ---------------------------------------------------------------------------
// CE FICHIER PORTE DEUX CHOSES DISTINCTES (à ne pas confondre)
// ---------------------------------------------------------------------------
// 1. Le SÉLECTEUR DE MEMBRE (modal, centré) = la vraie fenêtre du binaire
//    (UI_MemberSelectWnd). Fidèle : données = g_PartyRosterNames (10 slots x 13 o,
//    0x1674608 -> game::g_World.partyRoster.names), sélection, Fermer/Valider,
//    émissions Op57/Op58. C'est l'apport de la vague W6.
// 2. Le PANNEAU HUD « raid frame » (haut-gauche, PV/PM des coéquipiers) = une
//    INVENTION pragmatique SANS équivalent 1:1 dans le binaire, héritée d'une
//    session antérieure et honnêtement documentée comme telle à l'époque. Conservé
//    tel quel (pas de refactor opportuniste), mais sa garde de visibilité est
//    corrigée : elle ne dépend PLUS de Var(0x184BE40) (qui signifie « sélecteur de
//    membre ouvert », rien à voir), seulement de la présence d'au moins un membre
//    résolu — ce qui est exactement le contrat que ce panneau se donnait déjà
//    (« TOUJOURS AFFICHÉE tant que le groupe a au moins un membre résolu »).
//    Rappel de l'audit d'origine, toujours valable : la position haut-gauche n'est
//    PAS prouvée par le désassemblage ; les indicateurs de coéquipiers du vrai
//    UI_GameHud_Render sont des marqueurs projetés en espace monde (WorldToScreen),
//    pas un panneau HUD fixe. Ne pas re-documenter cette position comme « réelle ».
//
// ---------------------------------------------------------------------------
// CÂBLAGE MANQUANT (fichiers NON possédés par cette vague — à faire suivre)
// ---------------------------------------------------------------------------
// OpenMemberSelect()/Toggle() sont fidèles et complets, mais AUCUN appelant ne les
// atteint aujourd'hui. Les 3 déclencheurs du binaire vivent dans des fichiers non
// possédés par cette vague :
//   (a) Net/GameHandlers_PartyGuild.cpp (handler 0x3e Net_OnPartyMemberNameSet
//       0x4909A0) doit appeler Party().OpenMemberSelect() — ancre 0x4909F8. C'est
//       l'AMORCE de la pagination : sans elle, Op57 n'est jamais relancé.
//   (b) UI/GameHud.cpp — bouton de barre d'outils, ancre 0x678139 (UI_GameHud_OnClick :
//       UI_CloseAllDialogs(dword_1821D4C, 1) puis _Open/_Close) -> Party().Toggle().
//   (c) UI/GameWindows.cpp — raccourci clavier (code file UI 33, ancre 0x667730)
//       -> Party().Toggle(). Aucun hotkeys::kParty n'existe à ce jour.
// Tant que (a) n'est pas câblé, Net/GameHandlers_PartyGuild.cpp:193 (Net_SendOp57 de
// pagination) reste inatteignable MÊME si Var(0x184BE40) est désormais écrit : le
// handler 0x3f n'est relancé que par la boucle amorcée en 0x3e.
// NB ordre d'enregistrement : UI/GameWindows.cpp:59 enregistre party_ en FIN de liste
// (= rendu en premier/au fond, routé en dernier). Correct pour le raid frame, mais le
// sélecteur de membre est MODAL et devrait être routé/rendu en tête. À arbitrer par
// l'orchestrateur (fichier non possédé).
//
// ---------------------------------------------------------------------------
// Sources de données (le sélecteur ÉCRIT ; le raid frame est en LECTURE SEULE)
// ---------------------------------------------------------------------------
//   - game::g_World.partyRoster.names : miroir de g_PartyRosterNames 0x1674608
//                                       (10 slots x 13 o) — source du sélecteur.
//   - game::g_World.self             : PV/PM réels du joueur local (StatEngine).
//   - game::g_World.players[i]       : entités joueur visibles (dword_1687234,
//                                       stride 908, index 0 = self).
//   - game::g_Client.Var(adresse)    : miroir des champs de g_MemberSelectWnd
//                                       (cf. layout ci-dessus) et des PV membres :
//       dword_1687458 + 908*i         : PV courants du membre au slot joueur i
//       dword_168745C + 908*i         : PV max du membre au slot joueur i
//                                        (PartyMemberHpSet 0x7f / PartyMemberUpdate 0x80).
//
// LIMITATIONS FIDÈLES (pas d'invention) :
//   - Le sélecteur est indexé par SLOT DE ROSTER (0..9), exactement comme le binaire
//     (g_PartyRosterNames) : aucune jointure douteuse n'intervient sur le chemin
//     réseau — Op57/Op58 transportent un index de roster, pas un index d'entité.
//   - Le raid frame, lui, recoupe encore players[i] <-> partyRoster.names[i] en
//     best-effort : AUCUNE preuve RE que le slot de roster (assigné par le serveur)
//     coïncide avec l'index d'entité. Repli sur un libellé générique si vide.
//   - Barre MP des autres membres : PartyMemberHpSet écrit la MÊME paire d'adresses
//     (dword_1687458/168745C) pour PV et PM : aucune adresse distincte n'existe pour
//     le PM des AUTRES membres. Barre grisée plutôt qu'inventée.
//   - Géométrie du sélecteur : les OFFSETS sont prouvés (slots (x+17, y+81+20*i),
//     Fermer (x+252, y+24), Valider (x+214, y+298)) ; les DIMENSIONS du panneau et
//     des boutons proviennent de sprites .IMG résolus au runtime
//     (Sprite2D_GetWidth(unk_90265C)) et ne sont PAS connues statiquement -> valeurs
//     déduites, marquées TODO dans le .cpp.
#pragma once
#include "UI/UIManager.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"

#include <string>
#include <vector>

namespace ts2::ui {

// PartyWindow — porte (1) le sélecteur de membre modal (fidèle, UI_MemberSelectWnd)
// et (2) le panneau HUD « raid frame » auto-masqué (invention conservée).
class PartyWindow : public Dialog {
public:
    PartyWindow() = default;

    // --- Sélecteur de membre (fenêtre de groupe réelle du binaire) ---

    // UI_MemberSelectWnd_Open 0x667260 : refus en zone d'arène (message 1352), sinon
    // bOpen=1, reset des latches/sélection/valeurs, puis Net_SendOp57(premier slot de
    // roster NON VIDE) — un seul envoi (le binaire `return` dans la boucle, 0x667344).
    //
    // POURQUOI PAS `Open()` (override) ? UI/GameWindows.cpp:78 appelle `party_.Open()`
    // dès l'Init, au titre de l'ANCIEN contrat « panneau toujours visible, ouvert une
    // fois » (le raid frame inventé). Mapper UI_MemberSelectWnd_Open sur `Open()`
    // ferait donc apparaître le sélecteur MODAL à l'entrée en jeu — une régression
    // visible que cette vague n'a pas le droit de corriger (GameWindows.cpp n'est pas
    // possédé). L'ouverture fidèle porte donc un nom explicite ; `Open()` reste le
    // Dialog::Open() par défaut (inerte : Render() recalcule bOpen_ à chaque frame).
    // À FAIRE SUIVRE : supprimer GameWindows.cpp:78, devenu sans objet (la visibilité
    // du raid frame ne dépend plus d'un Open()).
    void OpenMemberSelect();

    // UI_MemberSelectWnd_Close 0x667350 : bOpen=0 (rien d'autre).
    void CloseMemberSelect();

    // Close() est branché sur CloseMemberSelect() : c'est FIDÈLE — le binaire ferme
    // tous les dialogues (UI_CloseAllDialogs 0x5AC590, ancre 0x6677C4) avant d'ouvrir
    // le sélecteur, donc UIManager::CloseAll/ResetAll doit bien le refermer.
    void Close() override;

    // UI_MemberSelectWnd_ProcNet 0x667730 (code file UI 33) : bascule. Ouvre seulement
    // si g_SceneMgr==6 && g_SceneSubState==4 (en jeu) — cf. .cpp pour la garde portée.
    // AUCUN APPELANT à ce jour (cf. bandeau « câblage manquant » ci-dessus).
    void Toggle();

    bool MemberSelectOpen() const; // Var(0x184BE40) != 0

    // --- Routage (le sélecteur modal a la priorité sur le raid frame) ---
    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override { (void)vk; return false; } // aucun raccourci interne prouvé

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    // ------------------------------------------------------------------
    // Sélecteur de membre (UI_MemberSelectWnd)
    // ------------------------------------------------------------------
    struct MsLayout {
        int x = 0, y = 0, w = 0, h = 0;
    };
    MsLayout ComputeMsLayout(int screenW, int screenH) const;
    // Rectangles dérivés des offsets PROUVÉS (cf. bandeau).
    void MsSlotRect(const MsLayout& m, int i, int& rx, int& ry, int& rw, int& rh) const;
    void MsCloseRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const;
    void MsConfirmRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const;

    void RenderMemberSelect(const UiContext& ctx, int cursorX, int cursorY);

    // Latches de boutons (champs +12/+16 de g_MemberSelectWnd : 0x184BE44/0x184BE48).
    // Portés en membres plutôt qu'en Var() : purement internes à la fenêtre, aucun
    // autre fichier ne les lit (contrairement à +8/+20/+24, exposés via Var()).
    bool msCloseLatch_   = false; // *(this+3) — 0x6674EE / 0x6675EF
    bool msConfirmLatch_ = false; // *(this+4) — 0x66753D / 0x66764E

    // Dims écran mémorisées au dernier Render : OnMouseDown/OnClick sont routés ENTRE
    // deux frames et ne reçoivent que (x,y), or le binaire recentre la fenêtre à chaque
    // événement (0x667394 / 0x6675A2). Même idiome que MsgBoxDialog::lastScreenW_.
    mutable int lastScreenW_ = ts2::kRefWidth;  // nWidth  0x1669184
    mutable int lastScreenH_ = ts2::kRefHeight; // nHeight 0x1669188

    // ------------------------------------------------------------------
    // Panneau HUD « raid frame » (invention conservée, cf. bandeau)
    // ------------------------------------------------------------------
    struct RowLayout {
        std::string name;
        int nameY = 0;
        int hpX = 0, hpY = 0, hpW = 0, hpH = 0;
        int hp = 0, hpMax = 0;
        int mpX = 0, mpY = 0, mpW = 0, mpH = 0;
        int mp = 0, mpMax = 0;
        bool hasMp = false; // false => barre MP grisée (aucune donnée, cf. bandeau .h)
    };

    struct Layout {
        bool visible = false;
        int  x = 0, y = 0, w = 0, h = 0;
        std::vector<RowLayout> rows;
    };

    Layout BuildLayout(int screenW, int screenH) const;

    mutable int  lastX_ = 0, lastY_ = 0, lastW_ = 0, lastH_ = 0;
    mutable bool lastVisible_ = false;

    static constexpr int kMaxRows  = 8;
    static constexpr int kPanelW   = 210;
    static constexpr int kMarginX  = 12;  // ancrage haut-gauche (NON prouvé, cf. bandeau)
    static constexpr int kMarginY  = 12;
    static constexpr int kPadX     = 10;
    static constexpr int kPadY     = 8;
    static constexpr int kTitleH   = 18;
    static constexpr int kNameH    = 13;
    static constexpr int kBarH     = 7;
    static constexpr int kBarGapY  = 2;
    static constexpr int kRowGapY  = 6;

    // ------------------------------------------------------------------
    // Adresses d'origine (miroir via game::ClientRuntime::Var)
    // ------------------------------------------------------------------
    // Champs de l'objet g_MemberSelectWnd 0x184BE38 (layout prouvé, cf. bandeau) :
    static constexpr uint32_t kVarMemberSelectOpen = 0x184BE40; // +8  bOpen
    static constexpr uint32_t kVarSelectedSlot     = 0x184BE4C; // +20 slot sélectionné
    static constexpr uint32_t kVarSlotValuesBase   = 0x184BE50; // +24 valeurs[10]
    // Divers :
    static constexpr uint32_t kVarWhisperPreset    = 0x184C64C; // g_WhisperPresetSlot (0x66747C)
    // (g_SelfMorphNpcId 0x1675A98 et g_SysMsgColor 0x84DFD8 sont adressés directement
    //  par les helpers d'espace anonyme du .cpp — pas de doublon de constante ici.)
    // Raid frame (invention) :
    static constexpr uint32_t kVarMemberHpBase     = 0x1687458; // dword_1687458[227*i]
    static constexpr uint32_t kVarMemberHpMaxBase  = 0x168745C; // dword_168745C[227*i]
    static constexpr uint32_t kMemberStride        = 908;       // 227 dwords * 4 o

    static constexpr int kRosterSlots = 10;  // 0x6672D8 / 0x667300 : boucles j<10, k<10
    static constexpr int kSlotUnset   = -1;  // 0x6672D1 : *(this+5) = -1
    static constexpr int kValueUnset  = -2;  // 0x6672F6 : *(this+j+6) = -2

    // --- Géométrie du sélecteur : offsets PROUVÉS (cf. bandeau .h) ---
    static constexpr int kMsSlotDX   = 17;   // 0x667438 : *this + 17
    static constexpr int kMsSlotDY0  = 81;   // 0x667438 : *(this+1) + 20*i + 81
    static constexpr int kMsSlotStep = 20;
    static constexpr int kMsCloseDX  = 252;  // 0x6674D2 : *this + 252
    static constexpr int kMsCloseDY  = 24;
    static constexpr int kMsOkDX     = 214;  // 0x667521 : *this + 214
    static constexpr int kMsOkDY     = 298;
    // --- Dimensions DÉDUITES (sprites .IMG résolus au runtime, cf. bandeau .h) ---
    static constexpr int kMsW = 280, kMsH = 330;
    static constexpr int kMsSlotW = 246, kMsSlotH = 18;
    static constexpr int kMsCloseW = 20, kMsCloseH = 20;
    static constexpr int kMsOkW = 52, kMsOkH = 22;

    // --- Identifiants StrTable005 (prouvés) ---
    static constexpr int kStrArenaRefused = 1352; // 0x667287 : refus en arène
    static constexpr int kStrNoSelection  = 529;  // 0x6676A4 : « aucun membre sélectionné »
    static constexpr int kStrConfirmBody  = 530;  // 0x6676CA : corps de la confirmation (MsgBox 21)

    static constexpr D3DCOLOR kColBg      = Argb(224, 32, 32, 40);
    static constexpr D3DCOLOR kColBorder  = Argb(255, 128, 128, 128);
    static constexpr D3DCOLOR kColTitle   = Argb(255, 255, 221, 102);
    static constexpr D3DCOLOR kColText    = Argb(255, 255, 255, 255);
    static constexpr D3DCOLOR kColHpBg    = Argb(200, 60, 20, 20);
    static constexpr D3DCOLOR kColHpFill  = Argb(255, 224, 64, 64);
    static constexpr D3DCOLOR kColMpBg    = Argb(200, 20, 24, 60);
    static constexpr D3DCOLOR kColMpFill  = Argb(255, 64, 96, 224);
    static constexpr D3DCOLOR kColNoData  = Argb(160, 70, 70, 70);
    static constexpr D3DCOLOR kColSelBg   = Argb(255, 64, 96, 160); // slot sélectionné
    static constexpr D3DCOLOR kColSlotBg  = Argb(255, 48, 48, 56);
    static constexpr D3DCOLOR kColBtnBg   = Argb(255, 64, 64, 72);
};

} // namespace ts2::ui

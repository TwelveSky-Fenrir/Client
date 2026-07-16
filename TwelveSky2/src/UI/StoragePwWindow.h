// UI/StoragePwWindow.h — fenêtre UI_StoragePwWnd (dword_1839950, ctor 0x666900).
//
// ###########################################################################
// # ATTENTION AU NOM : ce n'est PAS un « mot de passe d'entrepôt ».         #
// #                                                                         #
// # Le symbole IDB `UI_StoragePwWnd` est TROMPEUR. La fenêtre n'a AUCUN     #
// # champ de saisie : ni EditBox, ni caret, ni masquage. C'est une boîte à  #
// # TROIS BOUTONS SPRITE qui compare le mini-roster d'alliance              #
// # g_AllianceRosterNames 0x16749B8 au nom du joueur local byte_1673184 :   #
// #   bouton 1 (+87,+56)  = QUITTER l'alliance   (réservé aux NON-leaders)  #
// #   bouton 2 (+87,+110) = DISSOUDRE l'alliance (réservé au LEADER)        #
// #   bouton 3 (+87,+164) = FERMER                                          #
// # Le nom de fichier/classe suit le symbole IDA (règle « IDA = vérité      #
// # unique ») ; la SÉMANTIQUE est celle décrite ici.                        #
// ###########################################################################
//
// PREUVES D'IDENTITÉ (le dossier de gaps laissait l'identité « à élucider ») :
//   - `String` 0x7EC95F = CHAÎNE VIDE (read_cstring : length 0). Les
//     `Crt_Strcmp(g_AllianceRosterNames, &String)` sont donc des tests
//     « le slot 0 du roster est-il vide ? », pas des comparaisons de mot de passe.
//   - byte_1673184 = nom du joueur LOCAL. Preuve indépendante :
//     AutoPlay_ValidateChatName 0x45E670 fait
//     `for (i<5) if (strcmp(&g_AllianceRosterNames[13*i], "")) ++v3;
//      if (v3 != 1 || strcmp(roster[0], byte_1673184)) return 0; Net_SendOp71(...)`
//     -> roster[0] est comparé au joueur local pour décider « suis-je seul ? ».
//     Corroboré par Docs/TS2_ALLIANCE_PARTY_ROSTER.md:105.
//   - g_AllianceRosterNames[0] = LEADER (5 slots x 13 o) — modélisé côté C++ par
//     game::g_World.allianceRoster.memberNames[0] (Game/GameState.h:531-545).
//
// Fonctions d'origine transcrites (toutes re-prouvées par décompilation le 2026-07-16) :
//   UI_StoragePwWnd_Open        0x666960 -> Open()
//   UI_StoragePwWnd_Close       0x666A10 -> Close()
//   UI_StoragePwWnd_OnMouseDown 0x666A30 -> OnMouseDown()
//   UI_StoragePwWnd_OnClick     0x666BB0 -> OnClick()
//   UI_StoragePwWnd_ProcNet     0x666EB0 -> ProcKeyCommand()  [toggle clavier]
//   UI_StoragePwWnd_Render      0x666FE0 -> Render()
//
// ===========================================================================
// FAIT DÉCISIF — CETTE FENÊTRE N'ÉMET RIEN, JAMAIS (prouvé, cf. .cpp)
// ===========================================================================
// Ses deux actions se contentent d'ouvrir une boîte de confirmation
// `UI_MsgBox_Open(dword_1822438, 11|12, Str(364|365), "")`. Or :
//   - UI_MsgBox_Open 0x5C08C0 écrit le type en `this+24` = 0x1822450 ;
//     data_refs(0x1822450) = EXACTEMENT 10 lecteurs, couvrant les types
//     8, 9, 10, 14, 19, 20, 37, 38 — NI 11 NI 12 ;
//   - la jumptable d'acceptation 0x5C2DC3 range 11 et 12 dans son défaut
//     def_5C2DC3 0x5C2E8C = `mov eax, 1 ; retn 8` (pur épilogue).
// => Cliquer « Oui » sur la confirmation ne déclenche STRICTEMENT RIEN dans le
// binaire d'origine : aucun opcode, aucun effet. La fenêtre est une impasse
// cosmétique. La fidélité impose de reproduire CELA — surtout ne pas inventer
// d'émission (Op59/Op65/Op72 ou autre). Cela réduit fortement la sévérité
// réelle du gap USD-02 (annoncé « high »).
//
// Règle du projet : ce fichier n'édite AUCUN header existant.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"

#include <string>

namespace ts2::ui {

class StoragePwWindow : public Dialog {
public:
    StoragePwWindow();
    ~StoragePwWindow() override;

    // UI_StoragePwWnd_Open 0x666960 — DEUX refus AVANT toute ouverture :
    //   (a) Map_IsArenaZone() -> message StrTable005 #1352, RIEN d'autre (EA 0x66696E)
    //   (b) roster[0] vide    -> message StrTable005 #355,  RIEN d'autre (EA 0x6669A3)
    // Sinon : *(this+2)=1 puis boucle `for (i=0;i<3;++i) *(this+i+3)=0` (EA 0x6669DC).
    void Open() override;
    void Close() override;                    // UI_StoragePwWnd_Close 0x666A10

    bool OnMouseDown(int x, int y) override;  // UI_StoragePwWnd_OnMouseDown 0x666A30
    bool OnClick(int x, int y) override;      // UI_StoragePwWnd_OnClick     0x666BB0

    void Render(const UiContext& ctx, int cursorX, int cursorY) override; // 0x666FE0

    // -----------------------------------------------------------------------
    // API DE CÂBLAGE — le déclencheur clavier vit dans App/PlayerInputController.cpp
    // (vague W9, NON possédé par ce front). Chaîne prouvée dans le binaire :
    //   Camera_UpdateFromInput 0x50B7D0, case P (scancode DIK 25) @0x50DB80
    //     -> si mouse-look OFF : def_50C7A8 0x50DD5B -> call UI_RouteKeyInput @0x50DEC1
    //     -> UI_RouteKeyInput 0x5ADF50 @0x5AE124 -> UI_StoragePwWnd_ProcNet 0x666EB0.
    // Côté C++ ce chemin EXISTE DÉJÀ : App/PlayerInputController.cpp:151-152
    //   `case input::dik::kP:  if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }`
    // et `input::dik::kP` vaut 25 — exactement l'identifiant testé par ProcNet.
    // Ligne EXACTE à poser, dans PlayerInputController::RouteSceneKey(int dik) :
    //     if (ui::StoragePwWindow::ProcKeyCommand(dik)) return;   // 0x5AE124 -> 0x666EB0
    //
    // ProcKeyCommand transcrit intégralement UI_StoragePwWnd_ProcNet 0x666EB0 (y
    // compris la garde de scène InGame et l'asymétrie ouverture/fermeture) et
    // renvoie true si la commande a été consommée (return 1 du binaire).
    // -----------------------------------------------------------------------
    static StoragePwWindow* Active();
    static bool             ProcKeyCommand(int cmdId);

    // Identifiant de commande clavier consommé (`g_UiCmdQueueRecords[5*i] == 25`,
    // EA 0x666F39 / 0x666FB5) = scancode DIK_P.
    static constexpr int kCmdToggle = 25;

private:
    struct Rect { int x, y, w, h; };

    // Recentrage écran refait à CHAQUE événement, comme le binaire :
    // Render (EA 0x667000/0x66703E), OnMouseDown (0x666A52/0x666A90),
    // OnClick (0x666BD2/0x666C10) :
    //   x = nWidth/2  - Sprite2D_GetWidth(unk_8F70D4)/2
    //   y = nHeight/2 - Sprite2D_GetHeight(unk_8F70D4)/2
    void RecomputeCenter(int screenW, int screenH);

    Rect PanelRect() const;
    Rect LeaveButtonRect() const;    // btn1 : (+87, +56)
    Rect DisbandButtonRect() const;  // btn2 : (+87, +110)   <- DESSIN / MOUSEDOWN
    Rect DisbandClickRect() const;   // btn2 : (+87, +100)   <- CLIC  (quirk binaire !)
    Rect CloseButtonRect() const;    // btn3 : (+87, +164)

    void HandleLeaveClick();         // UI_StoragePwWnd_OnClick, branche *(this+3)
    void HandleDisbandClick();       // UI_StoragePwWnd_OnClick, branche *(this+4)

    // --- Latches de boutons (+3, +4, +5 du dialogue d'origine) ---
    bool leaveLatch_    = false;  // +3 (EA 0x666ACF / 0x666C23)
    bool disbandLatch_  = false;  // +4 (EA 0x666B1C / 0x666D21)
    bool closeLatch_    = false;  // +5 (EA 0x666B69 / 0x666E18)

    // ----------------------------------------------------------------------
    // GÉOMÉTRIE — littéraux exacts du binaire, sauf l'étendue du panneau et la
    // taille des boutons (dérivées des sprites unk_8F70D4 / unk_8F7168 /
    // unk_8F7290 / unk_8F73B8, non connaissables statiquement) : replis.
    // ----------------------------------------------------------------------
    static constexpr int kBtnX       = 87;   // +87 pour les TROIS boutons
    static constexpr int kBtnLeaveY  = 56;   // EA 0x666AB3 / 0x666C4A / 0x6670B0
    static constexpr int kBtnCloseY  = 164;  // EA 0x666B4D / 0x666E42 / 0x66718F

    // ######################################################################
    // # QUIRK BINAIRE PROUVÉ — À REPRODUIRE TEL QUEL (règle de fidélité)   #
    // # Le bouton 2 n'a PAS la même bande selon l'événement :              #
    // #   Render      0x66711E -> +110                                      #
    // #   OnMouseDown 0x666B00 -> +110                                      #
    // #   OnClick     0x666D48 -> +100   <-- 10 px PLUS HAUT               #
    // # Ce n'est pas une coquille de décompilation : les trois EA sont      #
    // # distincts et lisibles. Conséquence réelle dans le jeu d'origine :   #
    // # la bande CLIQUABLE du bouton « dissoudre » est décalée de 10 px     #
    // # vers le haut par rapport à la bande DESSINÉE. On reproduit.        #
    // ######################################################################
    static constexpr int kBtnDisbandDrawY  = 110; // dessin + mousedown
    static constexpr int kBtnDisbandClickY = 100; // clic (quirk 0x666D48)

    static constexpr int kBtnW   = 96;
    static constexpr int kBtnH   = 28;
    static constexpr int kPanelW = 270;  // contient les boutons (87+96 = 183)
    static constexpr int kPanelH = 210;  // contient le bouton bas (164+28 = 192)

    // --- Identifiants StrTable005 (tous relevés par décompilation) ---
    static constexpr int kStrArenaRefused = 1352; // Open, EA 0x666987
    static constexpr int kStrNoAlliance   = 355;  // Open EA 0x6669C0 ; OnClick EA 0x666C84
    static constexpr int kStrLeaderCant   = 362;  // btn1, EA 0x666CC4
    static constexpr int kStrLeaveConfirm = 364;  // btn1 -> MsgBox type 11, EA 0x666CED
    static constexpr int kStrNotLeader    = 363;  // btn2, EA 0x666DC2
    static constexpr int kStrDisbandConf  = 365;  // btn2 -> MsgBox type 12, EA 0x666DEB

    // Types de MsgBox (UI_MsgBox_Open(dword_1822438, type, corps, titre)).
    // AUCUN consommateur n'accepte 11/12 — cf. bandeau de tête « FAIT DÉCISIF ».
    static constexpr int kMsgBoxLeave   = 11; // EA 0x666CFA
    static constexpr int kMsgBoxDisband = 12; // EA 0x666DF8

    // --- Palette (repli si les sprites ne sont pas résolus) ---
    static constexpr D3DCOLOR kColPanelBg  = 0xE0202028u;
    static constexpr D3DCOLOR kColFrame    = 0xFF808080u;
    static constexpr D3DCOLOR kColTitle    = 0xFFFFDD66u;
    static constexpr D3DCOLOR kColText     = 0xFFFFFFFFu;
    static constexpr D3DCOLOR kColBtnBg    = 0xFF3A3A46u;
    static constexpr D3DCOLOR kColBtnHover = 0xFF4060A0u;
    static constexpr D3DCOLOR kColBtnDown  = 0xFF5878C0u;
};

} // namespace ts2::ui

// UI/GuildWindow.h — Fenêtre « Guilde » : roster interne (50 membres) + actions de guilde.
//
// Dialogue ts2::ui::Dialog (UI/UIManager.h) branché sur ts2::game::g_Guild
// (Game/GuildSystem.h, déjà écrit — voir ce header pour le layout d'origine détaillé).
// Cette fenêtre LIT g_Guild via son API publique (CountMembers/AddMember) et dessine
// par-dessus les primitives UiContext.
//
// ===========================================================================
// RÉSEAU — pattern FIDÈLE (Passe 4 / vague W6)
// ===========================================================================
// Le binaire n'a qu'UN objet réseau, g_NetClient 0x8156A0, adressé en GLOBAL : les
// builders Net_Send* le lisent DIRECTEMENT sans jamais le recevoir en paramètre
// (Guild_AddMemberFromInput 0x66BCD0 @0x66bd5b appelle Net_SendOp76 sans socket).
// Cette fenêtre utilise donc net::GlobalNetClient() (Net/NetClient.h:67-68), renseigné
// par ConnectLoginServer/ConnectGameServer (Net/Login.cpp:131/313).
//
// CORRECTIF W6 (défaut prouvé de la Passe 3) : l'ancien couple Bind(net::NetClient*)/
// net_ est SUPPRIMÉ. Bind() n'était appelé NULLE PART dans la composition (vérifié :
// seul skillTree_.Bind(...) existe, UI/GameWindows.cpp:72) -> net_ restait toujours
// nul -> le `if (net_)` de ConfirmAdd() était du CODE MORT et le seul envoi guilde du
// projet ne partait jamais. GlobalNetClient() restaure le singleton du binaire sans
// injection de dépendance.
//
// ===========================================================================
// ÉCART DE DISPOSITION (assumé, déjà documenté en Passe 3 — inchangé ici)
// ===========================================================================
// La VRAIE fenêtre d'origine (UI_GuildMgrWnd_Open/OnClick/Render 0x667E20/0x668B70/
// 0x66A2E0, état g_Guild 0x1839968) est une machine à 5 pages (`*(this+426)` :
// 1=roster, 2=inviter, 3=annonce, 4=rang, 5=alliance) affichant 10 lignes par PAGE
// (`*(this+427)` = page 0..4) sur 50 slots — PAS un scroll continu comme ici — et
// route ses actions destructrices par une MsgBox de confirmation (UI_MsgBox_Open
// 0x5C08C0, dword_1822438) dont le relâchement (UI_MsgBox_OnLButtonUp 0x5C1170)
// exécute l'envoi. Cette fenêtre-ci est une réinvention pragmatique : seul le
// CENTRAGE écran est prouvé bit-exact (cf. UI/GuildWindow.cpp::ComputeGeometry).
// Les ÉMISSIONS et leurs GARDES, elles, sont reproduites fidèlement (voir le .cpp).
//
// Interactions :
//   - Liste scrollable des membres non vides (nom + rang), 10 lignes visibles sur 50
//     slots max, boutons de défilement ▲/▼. Clic sur une ligne = sélection
//     (`*(this+428)` d'origine, -1 = aucune).
//   - « Ajouter »   -> saisie du nom  -> Net_SendOp76           (op 0x4C)
//   - « Rang »      -> saisie du rang -> Net_SendGuarded_10     (Op75 sous-op 10)
//   - « Quitter »   -> Net_SendGuarded_4                        (Op75 sous-op 4)
//   - « Dissoudre » -> Net_SendGuarded_6                        (Op75 sous-op 6)
//   - « X » sur une ligne -> expulsion, Net_SendGuarded_8       (Op75 sous-op 8)
//   - « X » du bandeau titre -> ferme la fenêtre (Dialog::Close()).
//   Chaque action reproduit les gardes du binaire (maître / ligne sélectionnée /
//   cible != soi) et leurs messages de refus StrTable005 — détail et ancres EA
//   exactes dans UI/GuildWindow.cpp.
//
// Limitation assumée (documentée) : l'UIManager ne route que WM_KEYDOWN (OnKey(vk)),
// pas WM_CHAR — la saisie n'accepte donc que chiffres/majuscules/espace (les codes
// VK_0..VK_9 et VK_A..VK_Z coïncident avec leurs codes ASCII en Win32), pas d'accents/
// minuscules/ponctuation. Voir ChatWindow (module non-Dialog du projet) pour un exemple
// qui reçoit un vrai WM_CHAR via une route dédiée — non disponible ici.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "UI/UIManager.h"
#include "UI/Widgets.h"
#include "Game/GuildSystem.h"

namespace ts2::ui {

class GuildWindow : public Dialog {
public:
    GuildWindow();

    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    // --- Géométrie ---------------------------------------------------------
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };
    struct Geom {
        Rect panel, header, closeBtn;
        Rect listArea, scrollUp, scrollDown;
        Rect actionRow, addBtn, rankBtn, leaveBtn, dissolveBtn;
        Rect editBox, confirmBtn, cancelBtn;
        Rect feedbackArea;
    };

    // Mode de saisie courant — analogue dégradé de la page `*(g_Guild+426)` du binaire
    // (UI_GuildMgrWnd_OnClick 0x668B70) : None ~ page 1 (roster), AddMember ~ page 2
    // (inviter, 0x668E29 `*(this+426)=2`), SetRank ~ page 4 (rang, 0x6694F3
    // `*(this+426)=4`). Les pages 3 (annonce) et 5 (alliance) ne sont pas portées —
    // cf. TODO du .cpp.
    enum class InputMode { None, AddMember, SetRank };

    enum class PressedBtn { None, Close, Add, Rank, Leave, Dissolve,
                            ScrollUp, ScrollDown, Confirm, Cancel, Kick, Row };

    Geom ComputeGeometry(int screenW, int screenH) const;
    Rect RowRect(const Geom& g, int rowOnScreen) const;
    Rect KickRect(const Geom& g, int rowOnScreen) const;

    // Indices [0..49] des slots non vides (game::g_Guild.members[i].Empty()==false).
    std::vector<int> VisibleMemberIndices() const;
    int MaxScroll() const;

    // `*(g_Guild+28) == g_SelfName` (Crt_Strcmp, UI_GuildMgrWnd_OnClick 0x668B70
    // @0x668DEF/0x66935E/0x669706/0x66984C…) — cf. note de fidélité dans le .cpp.
    bool IsSelfGuildMaster() const;

    // Nom du membre sélectionné, ou "" si aucune ligne (`*(this+428) == -1`).
    std::string SelectedMemberName() const;

    void Confirm();      // valide la saisie courante selon mode_
    void CancelInput();  // referme la saisie sans envoyer

    void DoKick();       // « X » d'une ligne  -> Net_SendGuarded_8
    void DoLeave();      // « Quitter »        -> Net_SendGuarded_4
    void DoDissolve();   // « Dissoudre »      -> Net_SendGuarded_6

    void SetFeedback(const std::string& text, D3DCOLOR color);

    // --- État ----------------------------------------------------------------
    InputMode   mode_         = InputMode::None; // saisie active (cf. enum)
    int         scrollOffset_ = 0;     // décalage de défilement (lignes depuis le haut)
    int         selectedIdx_  = -1;    // `*(g_Guild+428)` : slot sélectionné, -1 = aucun
    EditBox     nameEdit_;             // champ de saisie (nom ou rang selon mode_)

    std::string feedback_;                  // dernier message local
    D3DCOLOR    feedbackColor_ = 0xFF60FF60u; // succès par défaut
    float       feedbackUntil_ = -1.0f;       // horodatage d'expiration (ctx.gameTimeSec)

    PressedBtn  pressedBtn_     = PressedBtn::None; // latch armé au OnMouseDown
    int         pressedKickIdx_ = -1;               // index membre visé par le "X" armé
    int         pressedRowIdx_  = -1;               // index membre visé par la ligne armée

    // Dims écran + horloge mémorisées au dernier Render (le hit-test, routé entre deux
    // frames, doit s'aligner sur la géométrie effectivement dessinée). Même pattern que
    // MsgBoxDialog::lastScreenW_/lastScreenH_ dans UIManager.cpp.
    mutable int   lastScreenW_     = ts2::kRefWidth;
    mutable int   lastScreenH_     = ts2::kRefHeight;
    mutable float lastGameTimeSec_ = 0.0f;

    // --- Constantes de géométrie (panneau ~300x284, 10 lignes visibles) ---
    static constexpr int kPanelW        = 300;
    static constexpr int kHeaderH       = 28;
    static constexpr int kVisibleRows   = 10;
    static constexpr int kRowH          = 18;
    static constexpr int kListGap       = 8;
    static constexpr int kActionH       = 26;
    static constexpr int kFeedbackH     = 16;
    static constexpr int kBottomMargin  = 10;
    static constexpr int kMargin        = 8;
    static constexpr int kCloseBtnSize  = 18;
    static constexpr int kScrollBtnSize = 16;
    static constexpr int kKickBtnSize   = 16;
    static constexpr int kPanelH        = kHeaderH + kListGap + kVisibleRows * kRowH +
                                           kListGap + kActionH + kListGap + kFeedbackH +
                                           kBottomMargin;
    static constexpr float kFeedbackDurationSec = 3.0f;

    // Longueur max de la saisie « rang » : GetWindowTextA(dword_1668FF4, this+1945, 5)
    // (UI_GuildMgrWnd_OnClick 0x668B70 @0x669e43) -> 5 octets NUL-terminés = 4 caractères.
    static constexpr int kRankMaxChars = 4;
};

} // namespace ts2::ui

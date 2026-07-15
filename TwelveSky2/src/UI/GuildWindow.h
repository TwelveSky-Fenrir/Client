// UI/GuildWindow.h — Fenêtre « Guilde » : roster interne (50 membres) + ajout/expulsion.
//
// Dialogue ts2::ui::Dialog (UI/UIManager.h) branché sur ts2::game::g_Guild
// (Game/GuildSystem.h, déjà écrit — voir ce header pour le layout d'origine détaillé
// et les TODO(send) déjà documentés côté module d'état). Cette fenêtre se contente de
// LIRE/MUTER g_Guild via son API publique (CountMembers/SelectNextMember/AddMember/
// RemoveMember) et de dessiner par-dessus les primitives UiContext.
//
// Interactions :
//   - Liste scrollable des membres non vides (nom + rang), 10 lignes visibles sur 50
//     slots max, boutons de défilement ▲/▼.
//   - Bouton « Ajouter » -> ouvre un champ de saisie (ts2::ui::EditBox, Widgets.h) ;
//     « Confirmer » appelle GuildRoster::AddMember(name, false) puis, si accepté,
//     Net_SendOp76(nc, name61) (Net/SendPackets.h:108) — CÂBLÉ réellement (audit
//     usage builders, 2026-07-14) : confirmé par décompilation IDA de
//     Guild_AddMemberFromInput 0x66BCD0 (seul appelant d'origine de Net_SendOp76,
//     xref unique 0x66bd5b) qui envoie exactement le nom lu dans l'edit-box après
//     filtrage mots bannis — CE builder n'a PAS d'autre usage dans le binaire
//     d'origine, donc le point d'ancrage ci-dessous est celui EXACT du client
//     original. Réseau : Bind(NetClient*) attache (optionnellement) la session,
//     à l'image de UI/ChatWindow.cpp/UI/WarehouseWindow.h (net_ nullable, no-op
//     tant que non lié — AUCUN crash, juste pas d'envoi serveur). NB intégration :
//     comme pour WarehouseWindow, Bind() lui-même n'est appelé nulle part dans la
//     composition actuelle (App/SceneManager, hors périmètre de cette mission —
//     cf. consigne "ne pas toucher Scene/SceneManager.*") ; il suffira d'ajouter
//     côté SceneManager `windows_->Guild().Bind(&net_->Client());` pour finaliser
//     le branchement live. « Annuler » referme la saisie sans rien envoyer.
//   - Bouton « X » sur chaque ligne -> GuildRoster::RemoveMember(index) (mutation locale
//     immédiate, cf. contrat de mission) — TODO(send) opcode d'expulsion non identifié.
//   - Bouton « X » du bandeau titre -> ferme la fenêtre (Dialog::Close()).
//
// Limitation assumée (documentée) : l'UIManager ne route que WM_KEYDOWN (OnKey(vk)),
// pas WM_CHAR — la saisie du champ « Ajouter » n'accepte donc que chiffres/majuscules/
// espace (les codes VK_0..VK_9 et VK_A..VK_Z coïncident avec leurs codes ASCII en Win32),
// pas d'accents/minuscules/ponctuation. Voir ChatWindow (module non-Dialog du projet)
// pour un exemple qui reçoit un vrai WM_CHAR via une route dédiée — non disponible ici.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "UI/UIManager.h"
#include "UI/Widgets.h"
#include "Game/GuildSystem.h"

namespace ts2::net { struct NetClient; }

namespace ts2::ui {

class GuildWindow : public Dialog {
public:
    GuildWindow();

    // Attache la session réseau (nullable — cf. commentaire de tête de fichier).
    // Pattern identique à UI/ChatWindow.cpp::Bind / UI/WarehouseWindow.h::Bind : pas
    // d'inclusion lourde Net/NetClient.h ici (forward-decl uniquement), le .cpp
    // fait l'inclusion.
    void Bind(net::NetClient* nc) { net_ = nc; }

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
        Rect actionRow, addBtn, editBox, confirmBtn, cancelBtn;
        Rect feedbackArea;
    };

    enum class PressedBtn { None, Close, Add, ScrollUp, ScrollDown, Confirm, Cancel, Kick };

    Geom ComputeGeometry(int screenW, int screenH) const;
    Rect RowRect(const Geom& g, int rowOnScreen) const;
    Rect KickRect(const Geom& g, int rowOnScreen) const;

    // Indices [0..49] des slots non vides (game::g_Guild.members[i].Empty()==false).
    std::vector<int> VisibleMemberIndices() const;
    int MaxScroll() const;

    void ConfirmAdd(); // valide le champ "Ajouter" -> GuildRoster::AddMember + TODO(send)
    void CancelAdd();  // referme la saisie sans envoyer

    // --- État ----------------------------------------------------------------
    bool        addMode_      = false; // saisie "Ajouter" active
    int         scrollOffset_ = 0;     // décalage de défilement (lignes depuis le haut)
    EditBox     nameEdit_;             // champ de saisie du nom à ajouter

    std::string feedback_;                  // dernier message local (ajout/expulsion)
    D3DCOLOR    feedbackColor_ = 0xFF60FF60u; // succès par défaut
    float       feedbackUntil_ = -1.0f;       // horodatage d'expiration (ctx.gameTimeSec)

    PressedBtn  pressedBtn_     = PressedBtn::None; // latch armé au OnMouseDown
    int         pressedKickIdx_ = -1;               // index membre visé par le "X" armé

    // Dims écran + horloge mémorisées au dernier Render (le hit-test, routé entre deux
    // frames, doit s'aligner sur la géométrie effectivement dessinée). Même pattern que
    // MsgBoxDialog::lastScreenW_/lastScreenH_ dans UIManager.cpp.
    mutable int   lastScreenW_     = ts2::kRefWidth;
    mutable int   lastScreenH_     = ts2::kRefHeight;
    mutable float lastGameTimeSec_ = 0.0f;

    net::NetClient* net_ = nullptr; // session réseau optionnelle (cf. Bind())

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
};

} // namespace ts2::ui

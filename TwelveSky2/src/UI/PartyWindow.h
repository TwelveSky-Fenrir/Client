// UI/PartyWindow.h — panneau HUD « Groupe » (ts2::ui).
//
// AUDIT POSITIONS (RE-VÉRIFICATION PAR DÉCOMPILATION FRAÎCHE, 2026-07-14) : l'ancrage
// HAUT-GAUCHE ci-dessous N'EST PAS PROUVÉ par le désassemblage — c'est une
// approximation raisonnable héritée d'une session antérieure, PAS une position
// relevée dans le binaire. Recherche exhaustive menée pour cet audit :
//   - AUCUNE fonction UI_PartyWin_*/UI_PartyHud_* n'existe dans l'IDB (contrairement
//     à UI_ClanWin_* pour la Guilde) : il n'y a PAS de fenêtre dédiée "Groupe".
//   - xrefs_to sur dword_1687458/168745C (PV/PV max membre, cf. plus bas) : les 10
//     SEULES références du binaire entier sont les writers réseau
//     (Net_OnPartyMemberHpSet/Update/ItemResult) + UN SEUL lecteur, UI_GameHud_OnClick
//     0x677160 (hit-test d'un CLIC) — AUCUN lecteur côté UI_GameHud_Render 0x67A3C0
//     (rendu). Ces globals ne sont donc PROUVÉS nulle part comme dessinant un panneau
//     HP/MP de groupe à l'écran.
//   - Le seul élément UI cliquable lié à dword_184BE40 (flag "groupe actif") trouvé
//     dans UI_GameHud_OnClick 0x678139 est un BOUTON D'ICÔNE de la barre d'outils
//     principale (Sprite2D_HitTest à this.x+0x129, this.y+0x13 — bande de boutons
//     Stockage/Groupe/Guilde/Coffre espacés visible dans le même désassemblage), qui
//     BASCULE UI_MemberSelectWnd (sélecteur de 10 noms, MODAL et CENTRÉ écran comme
//     UI_ClanWin, PAS ancré en coin — cf. UI_MemberSelectWnd_Render 0x667860 :
//     `*this = nWidth/2 - w/2`). L'ancre de la barre d'outils elle-même est projetée
//     via UI_ProjectSpriteToScreen(controller, 299, 764, 182, ...) dans
//     cGameHud_InitLayout 0x62A5B0 -> point de référence (299,764) sur 1024x768,
//     donc proche du BAS de l'écran, PAS du coin haut-gauche.
//   - Les indicateurs de coéquipiers visibles dans UI_GameHud_Render sont des
//     marqueurs PROJETÉS EN ESPACE MONDE (WorldToScreen via la matrice caméra,
//     xrefs sur g_EntityArray 0x1687234 aux offsets 0x67B13E/0x6831C3), donc
//     ancrés à la position 3D du joueur à l'écran, PAS à un panneau HUD fixe.
// CONCLUSION DE L'AUDIT : aucune position "vraie" alternative n'est prouvée par le
// désassemblage (le panneau groupe façon "raid frame" façon WoW dessiné ici est une
// INVENTION pragmatique sans équivalent 1:1 dans le binaire). Par honnêteté (pas
// d'invention d'une fausse certitude), la position HAUT-GAUCHE est CONSERVÉE telle
// quelle (valeur par défaut raisonnable, non intrusive) mais son statut passe de
// "supposé" à "supposé et EXPLICITEMENT NON CONFIRMABLE" — ne pas re-documenter
// cette position comme "réelle" dans une session future sans nouvelle preuve EA.
//
// Fenêtre HUD compacte, ancrée en HAUT-GAUCHE (best-effort, cf. audit ci-dessus),
// TOUJOURS AFFICHÉE tant que le groupe a au moins un membre résolu côté client —
// même motif que UI/QuestTrackerWindow.h (Dialog non modal, sans bouton de
// fermeture, visibilité recalculée à CHAQUE Render()).
//
// Sources de données (LECTURE SEULE, aucun envoi réseau) :
//   - game::g_World.self             : PV/PM réels du joueur local (StatEngine),
//                                       plus fiable que le miroir Var() ci-dessous.
//   - game::g_World.players[i]       : tableau des entités joueur visibles
//                                       (dword_1687234, stride 908, index 0 = self).
//   - game::g_Client.Var(adresse)    : miroir des globals dispersés écrits par
//                                       Net/GameHandlers_PartyGuild.cpp :
//       dword_184BE40                 : flag « groupe actif », LU comme garde par le
//                                        handler PartyMemberValueSet (opcode 0x3f :
//                                        `if (g_Client.VarGet(0x184BE40)) ...`) — sert
//                                        ici de garde de visibilité du panneau.
//       dword_1687458 + 908*i         : PV courants du membre au slot joueur i
//       dword_168745C + 908*i         : PV max du membre au slot joueur i
//                                        (PartyMemberHpSet 0x7f / PartyMemberUpdate 0x80 ;
//                                        ces opcodes sont group-only côté serveur —
//                                        contrairement au tableau players[] qui contient
//                                        TOUT joueur visible à proximité, cf. .cpp).
//
// LIMITATIONS FIDÈLES (pas d'invention) :
//   - Noms des membres : PartyMemberNameSet (0x3e) et PartyMemberClear (0x40) écrivent
//     désormais réellement game::g_World.partyRoster.names[slotIndex] (miroir C++ de
//     g_PartyRosterNames, cf. Game/GameState.h::PartyRoster). Le nom affiché pour la ligne
//     d'entité `i` (autre que soi) est lu en BEST-EFFORT à `partyRoster.names[i]` — AUCUNE
//     preuve RE que le slot de roster (assigné par le serveur) coïncide avec l'index
//     d'entité `i` (résolu par identité réseau via PartyMemberHpSet/Update, tableau
//     totalement indépendant). Repli sur un libellé générique si le slot correspondant
//     est vide.
//   - Barre MP des autres membres : PartyMemberHpSet écrit la MÊME paire d'adresses
//     Var (dword_1687458/168745C) que le PV ou le PM courant soit reçu (kind==1 ou
//     kind==2 -> même destination, cf. commentaire du handler) : il n'existe donc
//     aucune adresse distincte pour le PM des AUTRES membres dans l'état actuel du
//     handler. Seule la barre MP du joueur local (source réelle SelfState) est
//     affichée ; celle des autres membres est dessinée grisée/vide plutôt
//     qu'inventée.
#pragma once
#include "UI/UIManager.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"

#include <string>
#include <vector>

namespace ts2::ui {

// PartyWindow — panneau non modal, toujours visible si le groupe a des membres.
// bOpen_ (Dialog) est recalculé à chaque Render() à partir de l'état groupe ;
// pas de bouton de fermeture (conforme à la consigne de mission).
class PartyWindow : public Dialog {
public:
    PartyWindow() = default;

    // Consomme uniquement le clic tombant SUR le panneau actuellement dessiné
    // (évite le clic-traversant vers le monde 3D sous ce HUD), même motif que
    // QuestTrackerWindow — panneau d'information pure, pas d'interaction.
    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override { (void)vk; return false; }

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    // Une ligne de membre déjà résolue en géométrie + valeurs prêtes à dessiner.
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

    // Recalcule membres + géométrie à partir de game::g_World / game::g_Client.
    // Appelé aux DEUX phases de Render (Panels/Text) — résultat identique dans
    // la même frame (pas de dépendance d'ordre), comme QuestTrackerWindow.
    Layout BuildLayout(int screenW, int screenH) const;

    // Dernière géométrie dessinée, mémorisée pour le hit-test (OnMouseDown/OnClick
    // sont routés entre deux frames, donc APRÈS le dernier Render).
    mutable int  lastX_ = 0, lastY_ = 0, lastW_ = 0, lastH_ = 0;
    mutable bool lastVisible_ = false;

    static constexpr int kMaxRows  = 8;   // consigne de mission : jusqu'à 8 lignes
    static constexpr int kPanelW   = 210;
    static constexpr int kMarginX  = 12;  // ancrage haut-gauche
    static constexpr int kMarginY  = 12;
    static constexpr int kPadX     = 10;
    static constexpr int kPadY     = 8;
    static constexpr int kTitleH   = 18;
    static constexpr int kNameH    = 13;
    static constexpr int kBarH     = 7;
    static constexpr int kBarGapY  = 2;
    static constexpr int kRowGapY  = 6;

    // Adresses des globals dispersés d'origine (miroir via game::ClientRuntime::Var),
    // cf. Net/GameHandlers_PartyGuild.cpp (bandeau .h ci-dessus pour le détail).
    static constexpr uint32_t kVarPartyActive     = 0x184BE40; // dword_184BE40
    static constexpr uint32_t kVarMemberHpBase    = 0x1687458; // dword_1687458[227*i]
    static constexpr uint32_t kVarMemberHpMaxBase = 0x168745C; // dword_168745C[227*i]
    static constexpr uint32_t kMemberStride       = 908;       // 227 dwords * 4 o

    static constexpr D3DCOLOR kColBg      = Argb(224, 32, 32, 40);   // ~0xE0202028
    static constexpr D3DCOLOR kColBorder  = Argb(255, 128, 128, 128); // ~0xFF808080
    static constexpr D3DCOLOR kColTitle   = Argb(255, 255, 221, 102); // ~0xFFFFDD66
    static constexpr D3DCOLOR kColText    = Argb(255, 255, 255, 255); // ~0xFFFFFFFF
    static constexpr D3DCOLOR kColHpBg    = Argb(200, 60, 20, 20);
    static constexpr D3DCOLOR kColHpFill  = Argb(255, 224, 64, 64);   // ~0xFFE04040
    static constexpr D3DCOLOR kColMpBg    = Argb(200, 20, 24, 60);
    static constexpr D3DCOLOR kColMpFill  = Argb(255, 64, 96, 224);   // ~0xFF4060E0
    static constexpr D3DCOLOR kColNoData  = Argb(160, 70, 70, 70);    // barre MP grisée
};

} // namespace ts2::ui

// UI/QuestTrackerWindow.h — panneau de suivi de quête (ts2::ui).
//
// Fenêtre HUD compacte, ancrée en haut-droite, TOUJOURS AFFICHÉE tant qu'une
// étape de quête est active côté client. Branchée sur Game/QuestSystem.h
// (déjà écrit) :
//   - CurrentQuestStepRecord() / SetCurrentQuestStepRecord() : record d'étape
//     mis en cache (mirroir de g_pCurQuestStepRecord / dword_18231B4). Sert de
//     GARDE de visibilité (nullptr => panneau masqué) et de source des champs
//     numériques de l'objectif (targetId/required/category/reward[3]) — ce
//     record NE PORTE AUCUN nom/texte libre (cf. bandeau QuestSystem.h : la
//     table QuestTbl (A) qui a des noms et la table NPC mQUEST (B) d'où vient
//     ce record sont DEUX SOURCES DISTINCTES, aucune jointure connue entre
//     elles). Faute de nom/texte, le panneau affiche les identifiants bruts
//     (zone/quête NPC) — pas de texte inventé.
//   - Quest_CheckObjectiveState / Quest_IsObjectiveComplete / Quest_GetRewardItemId
//     : évaluées sur un ts2::game::QuestProgressState LOCAL à cette fenêtre
//     (aucune instance globale n'est exposée par QuestSystem.h/ClientRuntime.h
//     à la date d'écriture). Exposé via Progress() pour un branchement futur
//     par le système qui maintiendra l'état réel du joueur (zoneId,
//     npcQuestId, objectiveMode/Type/Target/Progress) — même pattern
//     d'injection que QuestStepLookup dans QuestSystem.h.
//
// TODO PRECIS (state, pas réseau) : brancher Progress() sur le futur miroir
// client des offsets +10249/+10254/+11553..+11557 de g_PlayerCmdController
// (0x1669170), mis à jour par les handlers Pkt_* concernés (progression de
// quête, kill-track) — hors périmètre de cette mission UI.
//
// ---------------------------------------------------------------------------------------
// AUCUNE ÉMISSION RÉSEAU — PROUVÉ, PAS SUPPOSÉ (Passe 4 / vague W6, front `quest-npcdialog`).
// Le suivi de quête est PUREMENT LOCAL côté binaire. Les deux seules fonctions d'origine de
// ce panneau ne contiennent AUCUN appel Net_Send* (vérifié sur la liste de références de leur
// décompilation, pas sur une lecture approximative) :
//   Quest_DrawTracker      0x510FC0 — rendu pur : SkillDefTbl_GetRecord(mNPC, this+51588) ->
//     Sprite2D_Draw(&g_AssetMgr_UiAtlasSlots + 148*rec[1320] - 148) ; NpcTbl_FindByTypeAndId
//     (mQUEST, this+40996, this+46212 (+1 si this+51584==1)) ; 4x UI_DrawNumberValue aux
//     y+3/+19/+35/+51 ; formats "%s (%d,%d)" (GInfo_CalcRightMargin/CalcLeftMargin/
//     FindMotionByFrameId + StrTable003_Get) et "%s!" (this+40980). Ancre verticale :
//     dword_184C648==1 ? this+24-352 : this+24-196.
//   Quest_UpdateMarkerTimer 0x510D90 — état + audio seulement : garde !Map_IsArenaZone()
//     (0x54B690), extinction du marqueur à 30 s, réévaluation à 600 s, Quest_CheckObjectiveState
//     -> NpcTbl_FindByTypeAndId(mQUEST, ...) ; Snd3D_PlayScaledVolume(flt_148CABC,...,100,1) si
//     code==1, sinon this+51592 = Rng_Next()%3+1.
// => Ne rien émettre depuis ce panneau est CORRECT et FIDÈLE. Ce n'est pas une lacune de
// portage : ajouter un envoi ici serait une INVENTION. (Ancienne formulation non ancrée
// « Aucune action de ce panneau n'envoie de paquet réseau (lecture seule). » remplacée par
// les deux EA ci-dessus, qui la démontrent.)
// ---------------------------------------------------------------------------------------
#pragma once
#include "UI/UIManager.h"
#include "Game/QuestSystem.h"
#include "Game/GameDatabase.h"

#include <string>

namespace ts2::ui {

// QuestTrackerWindow — Dialog non modal, sans bouton de fermeture : sa
// visibilité (bOpen_) est recalculée à CHAQUE Render() à partir de
// game::CurrentQuestStepRecord() (auto-masqué si aucune quête active), donc
// un Open()/Close() externe n'a pas d'effet durable — conforme à la
// consigne « toujours visible si une quête est active ».
class QuestTrackerWindow : public Dialog {
public:
    QuestTrackerWindow() = default;

    // État de progression joueur consommé par Quest_IsObjectiveComplete /
    // Quest_GetRewardItemId / Quest_CheckObjectiveState. Le futur système
    // d'état joueur écrit ici (zoneId, npcQuestId, objectiveMode/Type/
    // Target/Progress) à chaque tick / paquet pertinent.
    game::QuestProgressState&       Progress()       { return progress_; }
    const game::QuestProgressState& Progress() const { return progress_; }
    void SetProgressState(const game::QuestProgressState& s) { progress_ = s; }

    // Événements souris : consomme uniquement le clic tombant SUR le panneau
    // (évite le clic-traversant vers le monde 3D sous ce HUD, cf. règle
    // « premier consommateur gagne » de UIManager) ; ne fait rien d'autre
    // (pas de bouton — panneau d'information pure).
    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    // Pas de saisie clavier consommée par ce panneau.
    bool OnKey(int vk) override { (void)vk; return false; }

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Layout {
        bool visible = false;
        int  x = 0, y = 0, w = 0, h = 0;
        std::string line1;      // identifiant quête (zone/npcQuestId)
        std::string line2;      // catégorie / type d'interaction
        std::string line3;      // objectif (cible + progression/requis)
        std::string line4;      // état (texte)
        D3DCOLOR    line4Color = 0xFFFFFFFFu;
        std::string line5;      // récompense potentielle
        bool        rewardActive = false;
    };

    // Recalcule géométrie + textes à partir de CurrentQuestStepRecord() +
    // progress_. Appelé au début des DEUX phases de Render (Panels/Text) —
    // pas de dépendance d'ordre, résultat identique dans la même frame.
    Layout BuildLayout(int screenW, int screenH) const;

    game::QuestProgressState progress_{};

    // Dernière géométrie dessinée, mémorisée pour le hit-test (OnMouseDown/
    // OnClick sont routés entre deux frames, donc APRÈS le dernier Render).
    mutable int lastX_ = 0, lastY_ = 0, lastW_ = 0, lastH_ = 0;
    mutable bool lastVisible_ = false;

    static constexpr int kPanelW   = 260;
    static constexpr int kMarginX  = 12;
    static constexpr int kMarginY  = 12;
    static constexpr int kPadX     = 10;
    static constexpr int kPadY     = 10;
    static constexpr int kLineH    = 16;
    static constexpr int kTitleH   = 18;

    static constexpr D3DCOLOR kColBg      = Argb(224, 32, 32, 40);   // ~0xE0202028
    static constexpr D3DCOLOR kColBorder  = Argb(255, 128, 128, 128); // ~0xFF808080
    static constexpr D3DCOLOR kColTitle   = Argb(255, 255, 221, 102); // ~0xFFFFDD66
    static constexpr D3DCOLOR kColText    = Argb(255, 255, 255, 255); // ~0xFFFFFFFF
    static constexpr D3DCOLOR kColSuccess = Argb(255, 96, 255, 96);   // ~0xFF60FF60
    static constexpr D3DCOLOR kColPending = Argb(255, 255, 221, 102); // en cours (= titre)
    static constexpr D3DCOLOR kColError   = Argb(255, 255, 96, 96);   // ~0xFFFF6060
};

} // namespace ts2::ui

// UI/NpcDialogWindow.h — fenêtre de dialogue PNJ (ts2::ui).
//
// Fenêtre modale simple ouverte lors d'une interaction avec un PNJ, affichant un texte
// contextuel selon que le PNJ est une cible de quête (Npc_IsQuestTarget, Game/NpcInteraction.h)
// et l'état de l'objectif de quête courant (Quest_CheckObjectiveState, Game/QuestSystem.h).
//
// TEXTE RÉEL (mission 2026-07-14 « CONTENU RÉEL DE NpcDialogWindow ») : le nom du PNJ et les
// lignes de dialogue viennent maintenant de Game/ExtraDatabases.h (NpcDefRecord::name/textGrid,
// table "mNPC" 005_00005.IMG, 500 records réels ; QuestDefRecord::dialogue, table "mQUEST"
// 005_00006.IMG, 1000 records réels, 10 blocs de dialogue/record). Ce ne sont PLUS des
// placeholders game::Str() — cf. bandeau .cpp (ResolveNpcName/BuildGreetingLines/
// BuildQuestLines) pour le détail des accesseurs et des hypothèses de sélection (page/bloc)
// qui restent, elles, non prouvées (aucune fonction d'origine ne correspond 1:1 à CETTE
// fenêtre synthétique — seules les DONNÉES sont réelles, pas l'algorithme de sélection).
//
// Seule action réseau motivée par cette fenêtre : le bouton « Accepter » ré-invoque
// NpcInteractionSystem::Interact() (Npc_Interact 0x53A660, déjà écrit dans
// Game/NpcInteraction.h et câblé sur host.SendVaultReq201 = Net_SendVaultReq_201 0x5901C0) —
// AUCUN nouveau point d'envoi n'est inventé ici.
//
// Règle du projet : ce fichier n'édite AUCUN header existant.
#pragma once
#include "UI/UIManager.h"
#include "Game/NpcInteraction.h"
#include "Game/QuestSystem.h"
#include "Game/ExtraDatabases.h"

#include <string>
#include <vector>

namespace ts2::ui {

// État de dialogue affiché — interprétation PRAGMATIQUE propre à cette fenêtre (pas un code
// renvoyé tel quel par un unique symbole du binaire) combinant Npc_IsQuestTarget et
// Quest_CheckObjectiveState/QuestProgressState::objectiveMode (cf. .cpp, ComputeState()) :
//   Generic         : PNJ non cible de quête (ou étape introuvable) -> accueil neutre.
//   Available       : cible de quête, aucun objectif "actif" suivi -> quête proposable.
//   InProgress      : objectif actif (mode==1), pas encore rempli (codes 2/4).
//   ReadyToComplete : objectif actif rempli (codes 3/5) -> prêt à remettre/terminer.
enum class NpcDialogState { Generic, Available, InProgress, ReadyToComplete };

class NpcDialogWindow : public Dialog {
public:
    NpcDialogWindow();

    // Ouvre la fenêtre pour le PNJ `npc`. Snapshot immédiat : aucun pointeur n'est conservé
    // au-delà de cet appel, hormis `interaction` qui doit rester valide tant que la fenêtre
    // peut être cliquée (même contrat que les autres systèmes hôtes du portage, ex.
    // NpcInteractionSystem::host.*).
    //   questCtx      : contexte élément/faction local pour Npc_IsQuestTarget.
    //   questProgress : état de progression courant pour Quest_CheckObjectiveState ; nullptr
    //                   -> état "Available" par défaut (aucune donnée de suivi disponible,
    //                   fallback prudent plutôt que d'inventer un état "en cours"/"terminé").
    //   interaction   : système d'interaction PNJ, pour le bouton "Accepter" (peut être
    //                   nullptr -> bouton visible mais inerte, no-op documenté, pas de crash).
    void Open(const game::NpcEntity& npc, const game::NpcQuestContext& questCtx,
              const game::QuestProgressState* questProgress,
              game::NpcInteractionSystem* interaction);
    void Open() override { Dialog::Open(); } // ouverture nue (défaut du contrat Dialog)
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    NpcDialogState State() const { return state_; }

private:
    struct Rect { int x, y, w, h; };
    Rect PanelRect() const;
    Rect AcceptButtonRect() const;
    Rect TalkButtonRect() const;
    Rect CloseButtonRect() const;
    bool PointInPanel(int mx, int my) const;

    void HandleAcceptClick();
    void HandleTalkClick();

    static std::string ResolveNpcName(const game::NpcEntity& npc, const game::NpcDefRecord* def);
    static NpcDialogState ComputeState(bool isQuestTarget, const game::QuestProgressState* progress);

    // Construit les lignes de texte à afficher depuis les VRAIES tables (cf. bandeau ci-dessus).
    // Retour vide si la donnée source est indisponible (table non chargée / record introuvable) ;
    // l'appelant (Render) affiche alors un repli littéral court (PAS un game::Str() inventé).
    static std::vector<std::string> BuildGreetingLines(const game::NpcDefRecord* def);
    static std::vector<std::string> BuildQuestLines(const game::QuestDefRecord* def, NpcDialogState state);

    game::EntityId               targetId_{};
    std::string                  npcName_ = "PNJ";
    NpcDialogState                state_ = NpcDialogState::Generic;
    game::NpcInteractionSystem*  interaction_ = nullptr;

    // Records réels résolus à Open() (durée de vie = celle de g_ExtraDb, chargée une fois pour
    // toutes par App_Init -> jamais rechargée en cours de partie ; pointeurs stables, même
    // convention que les ItemInfo*/records GameDatabase.h utilisés ailleurs dans ce portage).
    const game::NpcDefRecord*   npcDef_   = nullptr; // GetNpcDefRecord(npc.body[0]) — nullptr si absent
    const game::QuestDefRecord* questDef_ = nullptr; // GetQuestDefRecord(questProgress->npcQuestId) — idem

    bool  btnAcceptPressed_ = false;
    bool  btnTalkPressed_   = false;
    bool  btnClosePressed_  = false;

    std::string statusText_;      // dernier retour d'action (Accepter), affiché en pied de fenêtre
    float       lastGameTimeSec_ = 0.0f; // dernier ctx.gameTimeSec vu en Render (cf. MsgBoxDialog::lastScreenW_)

    static constexpr int kPanelW    = 420;
    static constexpr int kPanelH    = 200;
    static constexpr int kHeaderH   = 30;
    static constexpr int kPadX      = 14;
    static constexpr int kBtnW      = 110;
    static constexpr int kBtnH      = 26;
    static constexpr int kBtnGap    = 14;

    static constexpr D3DCOLOR kColBg       = 0xE0202028u; // fond panneau
    static constexpr D3DCOLOR kColBorder   = 0xFF808080u; // cadre
    static constexpr D3DCOLOR kColTitle    = 0xFFFFDD66u; // titre
    static constexpr D3DCOLOR kColText     = 0xFFFFFFFFu; // texte
    static constexpr D3DCOLOR kColTextDim  = 0xFFAAAAAAu; // texte atténué (ligne de quête)
    static constexpr D3DCOLOR kColHover    = 0xFF4060A0u; // survol
    static constexpr D3DCOLOR kColBtnBg    = 0xFF3A3A46u; // bouton actif
    static constexpr D3DCOLOR kColBtnOff   = 0xFF262629u; // bouton désactivé
    static constexpr D3DCOLOR kColHeaderBg = 0xFF2A2A34u; // bandeau titre
    static constexpr D3DCOLOR kColSuccess  = 0xFF60FF60u; // statut : action envoyée
};

} // namespace ts2::ui

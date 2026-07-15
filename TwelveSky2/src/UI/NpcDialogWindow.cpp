// UI/NpcDialogWindow.cpp — implémentation de la fenêtre de dialogue PNJ.
// Voir UI/NpcDialogWindow.h pour le contrat d'interaction et la note sur la provenance du
// texte réel (NpcDefRecord/QuestDefRecord, Game/ExtraDatabases.h — plus de placeholders Str()).
#include "UI/NpcDialogWindow.h"
#include "UI/PanelSkin.h"
#include <cstring>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit LARGE/COURT (468,128) du
// dossier atlas UI G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA,
// seul gabarit "bandeau large" identifié dans l'atlas, cohérent avec une boîte
// de dialogue PNJ (420x200, large et basse ; cf. méthodologie détaillée dans
// UI/PanelSkin.h). Repli automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_02463.IMG");
} // namespace

NpcDialogWindow::NpcDialogWindow() {
    // Centrage horizontal, ancré bas d'écran (convention "boîte de dialogue PNJ" — proche du
    // personnage, sous le HUD principal), référence 1024x768 comme les autres fenêtres du
    // portage (cf. WarehouseWindow::WarehouseWindow()).
    x_ = (ts2::kRefWidth  - kPanelW) / 2;
    y_ = ts2::kRefHeight - kPanelH - 90;
}

// ============================================================================
// Cycle de vie
// ============================================================================
void NpcDialogWindow::Open(const game::NpcEntity& npc, const game::NpcQuestContext& questCtx,
                            const game::QuestProgressState* questProgress,
                            game::NpcInteractionSystem* interaction) {
    Dialog::Open();

    targetId_ = npc.id;

    // Table "mNPC" réelle (005_00005.IMG, NpcDefRecord, cf. Game/ExtraDatabases.h) — indexée
    // par npc.body[0] ("mob id -> NPC/MobDb", même convention que StaticNpcLoader.cpp qui
    // résout GetNpcDefRecord(kindId) depuis le champ homologue de la table de PNJ statiques).
    // nullptr si la table n'est pas chargée ou si l'id est hors bornes/slot vide -> replis
    // gérés dans ResolveNpcName()/BuildGreetingLines() (jamais de crash, jamais d'invention).
    npcDef_  = npc.body.empty() ? nullptr : game::GetNpcDefRecord(npc.body[0]);
    npcName_ = ResolveNpcName(npc, npcDef_);

    // Table "mQUEST" réelle (005_00006.IMG, QuestDefRecord) — indexée par
    // QuestProgressState::npcQuestId (+11553, "id de l'enregistrement NPC (mQUEST) courant",
    // cf. Game/QuestSystem.h). Repli documenté : le binaire résout en réalité ce record via
    // NpcTbl_FindByTypeAndId(mQUEST, élément/type, npcQuestId) (0x4C8340, confirmé décompilé
    // dans UI_EventNoticeWnd_Open 0x6649F0) — une clé COMPOSITE (type+id), alors que
    // GetQuestDefRecord() ici n'indexe que par id (1-based, linéaire). Approximation FIDÈLE EN
    // PRATIQUE tant que les id de quête sont uniques toutes zones confondues (aucune preuve du
    // contraire observée), mais non prouvée bit-exacte — cf. TODO ci-dessous si des collisions
    // apparaissent en jeu.
    questDef_ = questProgress
                    ? game::GetQuestDefRecord(static_cast<uint32_t>(questProgress->npcQuestId))
                    : nullptr;

    // Npc_IsQuestTarget 0x540340 (Game/NpcInteraction.h) : opère sur le pointeur "def" du PNJ
    // (peut être nullptr -> false, fidèle : "résultat non trouvé", cf. commentaire d'origine).
    const bool isTarget = game::Npc_IsQuestTarget(npc.def, questCtx);
    state_ = ComputeState(isTarget, questProgress);

    interaction_ = interaction;
    statusText_.clear();
    btnAcceptPressed_ = btnTalkPressed_ = btnClosePressed_ = false;
}

void NpcDialogWindow::Close() {
    btnAcceptPressed_ = btnTalkPressed_ = btnClosePressed_ = false;
    Dialog::Close();
}

// ============================================================================
// Résolution nom PNJ / lignes de dialogue — DONNÉES RÉELLES (Game/ExtraDatabases.h)
// ============================================================================
std::string NpcDialogWindow::ResolveNpcName(const game::NpcEntity& npc, const game::NpcDefRecord* def) {
    // NpcDefRecord::name (+4, <=24 car. utiles) — vrai nom du PNJ décodé depuis 005_00005.IMG
    // (ex. rec[0] = "Blacksmith Wu"). `name` n'est pas garanti null-terminé si les 25 octets
    // sont tous utilisés (cf. commentaire du champ dans ExtraDatabases.h) -> construction
    // explicite bornée à sizeof(name) plutôt que std::string(def->name) brut (évite une lecture
    // hors-tableau si jamais le validateur amont laissait passer un nom plein).
    if (def) {
        const std::string name(def->name, strnlen(def->name, sizeof(def->name)));
        if (!name.empty()) return name;
    }
    // Repli : table absente/non chargée OU npcId hors bornes/slot vide -> libellé
    // générique plutôt qu'un faux texte localisé (pas de game::Str() ici).
    if (npc.body.empty() || npc.body[0] == 0) return "PNJ";
    return "PNJ";
}

namespace {
// Ajoute `line` à `out` si non vide après recopie bornée (les chaînes de table ne sont pas
// garanties null-terminées si le champ est plein — cf. commentaire NpcDefRecord::textGrid /
// QuestDefRecord::dialogue[].lines dans ExtraDatabases.h).
template <std::size_t N>
void AppendTableLine(std::vector<std::string>& out, const char (&field)[N]) {
    const std::string s(field, strnlen(field, N));
    if (!s.empty()) out.push_back(s);
}
} // namespace

// NpcDefRecord::textGrid — grille 5 "pages" x 5 lignes (hypothèse de layout, cf.
// ExtraDatabases.h). Aucune fonction d'origine ne sélectionne explicitement une page pour
// CETTE fenêtre synthétique (pas d'équivalent 1:1 dans le binaire) : repli PRAGMATIQUE sur la
// page 0 (première page de dialogue, cohérente avec un texte d'accueil), plafonné à 2 lignes
// pour tenir dans le panneau (420x200). Les DONNÉES sont réelles, le CHOIX de page ne l'est
// pas — documenté ici plutôt que caché.
std::vector<std::string> NpcDialogWindow::BuildGreetingLines(const game::NpcDefRecord* def) {
    std::vector<std::string> lines;
    if (!def) return lines;
    for (int row = 0; row < 5 && lines.size() < 2; ++row)
        AppendTableLine(lines, def->textGrid[0][row]);
    return lines;
}

// QuestDefRecord::dialogue[10] — 10 blocs de 15 lignes (confirmé par le commentaire
// désassemblage cité dans ExtraDatabases.h). Sélection de bloc par état PRAGMATIQUE (pas de
// preuve directe d'un mapping état->bloc pour cette fenêtre synthétique, cf. bandeau .h) :
// bloc 0 = accueil/proposition (Available), bloc 1 = rappel en cours (InProgress), bloc 2 =
// remise/fin (ReadyToComplete) — ordre naturel d'un scénario de quête à 3 étapes, plafonné à
// 2 lignes par bloc pour tenir dans le panneau. Generic n'affiche rien (PNJ non cible de
// quête -> pas de dialogue de quête à montrer).
std::vector<std::string> NpcDialogWindow::BuildQuestLines(const game::QuestDefRecord* def, NpcDialogState state) {
    std::vector<std::string> lines;
    if (!def) return lines;
    int block = -1;
    switch (state) {
        case NpcDialogState::Available:       block = 0; break;
        case NpcDialogState::InProgress:      block = 1; break;
        case NpcDialogState::ReadyToComplete: block = 2; break;
        case NpcDialogState::Generic:         block = -1; break;
    }
    if (block < 0 || block >= 10) return lines;
    for (int row = 0; row < 15 && lines.size() < 2; ++row)
        AppendTableLine(lines, def->dialogue[block].lines[row]);
    return lines;
}

NpcDialogState NpcDialogWindow::ComputeState(bool isQuestTarget,
                                              const game::QuestProgressState* progress) {
    if (!isQuestTarget) return NpcDialogState::Generic;
    if (!progress) return NpcDialogState::Available; // pas de données de suivi -> proposable

    if (progress->objectiveMode == 0) {
        // Branche "implicite" de Quest_CheckObjectiveState 0x50FF10 (mode/type/cible/progression
        // tous à 0 côté binaire) : le code renvoyé est un simple bool 0/1, PAS les codes 0..5 de
        // la branche "active" ci-dessous — les deux plages se recouvrent (0 existe dans les deux),
        // donc on lève l'ambiguïté nous-mêmes via objectiveMode (lu directement sur
        // QuestProgressState) plutôt que de deviner depuis le seul code retour.
        return game::Quest_CheckObjectiveState(*progress) != 0
                   ? NpcDialogState::Available : NpcDialogState::Generic;
    }

    // Branche "active" (mode==1, cf. QuestSystem.h) : 0=invalide/introuvable, 2=en cours,
    // 3=rempli (cases 1-5/8), 4=case 6.2 non rempli, 5=case 6.2 rempli.
    switch (game::Quest_CheckObjectiveState(*progress)) {
        case 2: case 4: return NpcDialogState::InProgress;
        case 3: case 5: return NpcDialogState::ReadyToComplete;
        default:        return NpcDialogState::Available; // 0 : rien de précis à afficher
    }
}

// ============================================================================
// Géométrie
// ============================================================================
NpcDialogWindow::Rect NpcDialogWindow::PanelRect() const { return { x_, y_, kPanelW, kPanelH }; }

NpcDialogWindow::Rect NpcDialogWindow::AcceptButtonRect() const {
    const int totalW = kBtnW * 3 + kBtnGap * 2;
    const int startX = x_ + (kPanelW - totalW) / 2;
    return { startX, y_ + kPanelH - kBtnH - 16, kBtnW, kBtnH };
}
NpcDialogWindow::Rect NpcDialogWindow::TalkButtonRect() const {
    const int totalW = kBtnW * 3 + kBtnGap * 2;
    const int startX = x_ + (kPanelW - totalW) / 2;
    return { startX + kBtnW + kBtnGap, y_ + kPanelH - kBtnH - 16, kBtnW, kBtnH };
}
NpcDialogWindow::Rect NpcDialogWindow::CloseButtonRect() const {
    const int totalW = kBtnW * 3 + kBtnGap * 2;
    const int startX = x_ + (kPanelW - totalW) / 2;
    return { startX + 2 * (kBtnW + kBtnGap), y_ + kPanelH - kBtnH - 16, kBtnW, kBtnH };
}

bool NpcDialogWindow::PointInPanel(int mx, int my) const {
    return PointInRect(mx, my, x_, y_, kPanelW, kPanelH);
}

// ============================================================================
// Événements souris / clavier
// ============================================================================
bool NpcDialogWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    // Consomme le clic-enfoncé s'il tombe dans le panneau (empêche qu'il retombe sur le monde
    // 3D, règle « premier consommateur gagne » de UIManager). Toute l'action est au relâchement
    // (OnClick), comme documenté dans le contrat Dialog.
    return PointInPanel(x, y);
}

bool NpcDialogWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    if (!PointInPanel(x, y)) return false;

    const bool canAccept = (state_ == NpcDialogState::Available ||
                             state_ == NpcDialogState::ReadyToComplete);

    const Rect acceptBtn = AcceptButtonRect();
    if (canAccept && PointInRect(x, y, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h)) {
        HandleAcceptClick();
        return true;
    }

    const Rect talkBtn = TalkButtonRect();
    if (PointInRect(x, y, talkBtn.x, talkBtn.y, talkBtn.w, talkBtn.h)) {
        HandleTalkClick();
        return true;
    }

    const Rect closeBtn = CloseButtonRect();
    if (PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        Close();
        return true;
    }

    // Clic dans le panneau mais hors zone active (fond, titre...) : consommé quand même, la
    // fenêtre est modale de fait pendant qu'elle est ouverte (même convention que WarehouseWindow).
    return true;
}

bool NpcDialogWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { Close(); return true; }
    return false;
}

// ============================================================================
// Actions
// ============================================================================
void NpcDialogWindow::HandleAcceptClick() {
    // Ré-interagit avec le PNJ ciblé : NpcInteractionSystem::Interact (Npc_Interact 0x53A660,
    // Game/NpcInteraction.h) rejoue la logique complète — portée (<=50.0), verrou morph/latch,
    // construction ET émission de la requête de récompense/vault via host.SendVaultReq201
    // (= Net_SendVaultReq_201 0x5901C0, déjà branché comme hook injectable dans
    // NpcInteractionSystem). AUCUN nouveau point d'envoi réseau n'est créé ici ; si `host.
    // SendVaultReq201` n'a pas été câblé par l'appelant, Interact() reste un no-op silencieux
    // (fidèle : "sinon silencieux, PAS d'erreur", cf. NpcInteraction.h).
    if (!interaction_) {
        statusText_ = "(systeme d'interaction non branche)";
        return;
    }
    interaction_->Interact(targetId_, lastGameTimeSec_);
    statusText_ = (state_ == NpcDialogState::ReadyToComplete)
                      ? "Quete terminee (requete envoyee)."
                      : "Quete acceptee (requete envoyee).";
    // La fenêtre reste ouverte (comme WarehouseWindow après un retrait) pour laisser voir le
    // statut ; le joueur ferme via "Fermer" (ou Échap). Pas de re-calcul de state_ ici : la
    // vraie mise à jour de progression arrivera par un futur handler réseau (hors périmètre).
}

void NpcDialogWindow::HandleTalkClick() {
    // "Parler" ne rejoue PAS Interact() : dans le binaire d'origine, Npc_Interact EST déjà
    // l'action de dialogue (approche + éventuelle requête serveur) — c'est elle qui a conduit à
    // l'ouverture de CETTE fenêtre en amont (hors périmètre de ce fichier, cf. mission). Ce
    // bouton reste donc purement local : il réaffiche la ligne de salut sans fermer la fenêtre
    // ni émettre quoi que ce soit. Pas de TODO(send) : aucune action réseau n'est esquivée ici,
    // il n'y en a simplement pas à ce stade côté "Parler".
    statusText_.clear();
}

// ============================================================================
// Rendu
// ============================================================================
void NpcDialogWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;
    lastGameTimeSec_ = ctx.gameTimeSec; // mémorisé pour HandleAcceptClick (routé hors Render)

    const Rect panel     = PanelRect();
    const Rect acceptBtn = AcceptButtonRect();
    const Rect talkBtn   = TalkButtonRect();
    const Rect closeBtn  = CloseButtonRect();
    const bool canAccept = (state_ == NpcDialogState::Available ||
                             state_ == NpcDialogState::ReadyToComplete);

    if (ctx.phase == UiPhase::Panels) {
        // Fond + cadre du panneau.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);

        // Bandeau titre (nom du PNJ).
        ctx.FillRect(panel.x, panel.y, panel.w, kHeaderH, kColHeaderBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kHeaderH, kColBorder, 1);

        // Bouton "Accepter" (grisé si aucune action de quête n'est disponible).
        const bool acceptHover = canAccept &&
            PointInRect(cursorX, cursorY, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h);
        ctx.FillRect(acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h,
                     !canAccept ? kColBtnOff : (acceptHover ? kColHover : kColBtnBg));
        ctx.DrawFrame(acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h, kColBorder, 1);

        // Bouton "Parler" (toujours actif).
        const bool talkHover =
            PointInRect(cursorX, cursorY, talkBtn.x, talkBtn.y, talkBtn.w, talkBtn.h);
        ctx.FillRect(talkBtn.x, talkBtn.y, talkBtn.w, talkBtn.h, talkHover ? kColHover : kColBtnBg);
        ctx.DrawFrame(talkBtn.x, talkBtn.y, talkBtn.w, talkBtn.h, kColBorder, 1);

        // Bouton "Fermer" (bouton de fermeture obligatoire de la fenêtre modale).
        const bool closeHover =
            PointInRect(cursorX, cursorY, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h);
        ctx.FillRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, closeHover ? kColHover : kColBtnBg);
        ctx.DrawFrame(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, kColBorder, 1);
        return;
    }

    // --- Phase texte ---
    ctx.Text(npcName_.c_str(), panel.x + kPadX, panel.y + (kHeaderH - 12) / 2, kColTitle);

    // Ligne(s) de salut — VRAI texte NpcDefRecord::textGrid (table "mNPC" 005_00005.IMG), cf.
    // BuildGreetingLines(). Repli littéral court (PAS de game::Str() inventé) si la table n'est
    // pas chargée ou si le PNJ n'a pas de NpcDefRecord résolu (npcDef_ == nullptr).
    int ty = panel.y + kHeaderH + 18;
    const int kLineH = 18;
    const std::vector<std::string> greetingLines = BuildGreetingLines(npcDef_);
    if (!greetingLines.empty()) {
        for (const std::string& line : greetingLines) {
            ctx.Text(line.c_str(), panel.x + kPadX, ty, kColText);
            ty += kLineH;
        }
    } else {
        ctx.Text("...", panel.x + kPadX, ty, kColTextDim);
        ty += kLineH;
    }

    // Ligne(s) d'état de quête — VRAI texte QuestDefRecord::dialogue (table "mQUEST"
    // 005_00006.IMG), seulement si le PNJ est une cible de quête (state_ != Generic). Repli
    // littéral court si questDef_ == nullptr (table non chargée / npcQuestId sans record) ou
    // si le bloc sélectionné pour cet état est vide dans la table.
    if (state_ != NpcDialogState::Generic) {
        const std::vector<std::string> questLines = BuildQuestLines(questDef_, state_);
        if (!questLines.empty()) {
            for (const std::string& line : questLines) {
                ctx.Text(line.c_str(), panel.x + kPadX, ty, kColTextDim);
                ty += kLineH;
            }
        } else {
            const char* fallback = "(quete)";
            switch (state_) {
                case NpcDialogState::Available:       fallback = "Quete disponible.";      break;
                case NpcDialogState::InProgress:      fallback = "Revenir voir plus tard."; break;
                case NpcDialogState::ReadyToComplete: fallback = "Terminer la quete.";      break;
                default: break;
            }
            ctx.Text(fallback, panel.x + kPadX, ty, kColTextDim);
            ty += kLineH;
        }
    }

    // Libellés de boutons — chrome UI fixe (pas du texte de dialogue localisé, donc pas de
    // game::Str() ici, comme "Options"/"Annuler"/"Entrepot" ailleurs dans ce portage).
    const char* acceptLbl = "Accepter";
    const int acceptLblW = ctx.MeasureText(acceptLbl);
    ctx.Text(acceptLbl, acceptBtn.x + (acceptBtn.w - acceptLblW) / 2, acceptBtn.y + 6,
              canAccept ? kColText : kColTextDim);

    const char* talkLbl = "Parler";
    const int talkLblW = ctx.MeasureText(talkLbl);
    ctx.Text(talkLbl, talkBtn.x + (talkBtn.w - talkLblW) / 2, talkBtn.y + 6, kColText);

    const char* closeLbl = "Fermer";
    const int closeLblW = ctx.MeasureText(closeLbl);
    ctx.Text(closeLbl, closeBtn.x + (closeBtn.w - closeLblW) / 2, closeBtn.y + 6, kColText);

    // Dernier statut d'action (Accepter), affiché au-dessus de la rangée de boutons.
    if (!statusText_.empty())
        ctx.Text(statusText_.c_str(), panel.x + kPadX, closeBtn.y - 20, kColSuccess);
}

} // namespace ts2::ui

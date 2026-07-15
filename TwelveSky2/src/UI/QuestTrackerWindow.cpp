// UI/QuestTrackerWindow.cpp — implémentation du panneau de suivi de quête.
// Voir UI/QuestTrackerWindow.h pour le contrat et les réserves sur les
// données affichées (deux sources de données quête distinctes, cf.
// Game/QuestSystem.h).
#include "UI/QuestTrackerWindow.h"
#include "UI/PanelSkin.h"
#include "Game/StringTables.h" // game::g_Strings.zoneNames (003.DAT -> mZONENAME)

#include <cstdarg>
#include <cstdio>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit étroit/haut (252,440), le PLUS
// répété (63 occurrences non consécutives) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, retenu par
// défaut pour ce panneau HUD étroit (260 px de large, hauteur dynamique selon
// les lignes affichées ; cf. méthodologie détaillée dans UI/PanelSkin.h).
// Indice distinct de celui utilisé par PartyWindow. Repli automatique sur
// kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00516.IMG");

// Formatage sans exceptions ni allocation dynamique excessive (snprintf ->
// std::string). `fmt` : format C classique.
std::string Fmt(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}
} // namespace

QuestTrackerWindow::Layout QuestTrackerWindow::BuildLayout(int screenW, int screenH) const {
    Layout L;

    // Garde de visibilité : CurrentQuestStepRecord() (mirroir g_pCurQuestStepRecord /
    // dword_18231B4). nullptr => aucune quête active => panneau entièrement masqué.
    const game::QuestStepRecord* npc = game::CurrentQuestStepRecord();
    if (!npc) return L; // L.visible reste false

    L.visible = true;

    // --- Identifiant de l'étape : nom de zone RÉEL si 003.DAT (mZONENAME) le connaît
    //     pour progress_.zoneId (StrTable003_Get 0x4C1AD0, index 1-based, cf.
    //     Game/StringTables.h) ; repli sur l'id numérique si la table est vide/non
    //     chargée ou si l'entrée n'existe pas (Get() renvoie "" hors bornes). Aucun
    //     nom de quête disponible (cf. bandeau .h : QuestTbl (A) et mQUEST (B) sont
    //     deux sources sans jointure connue) -> npcQuestId reste numérique.
    const char* zoneName = game::g_Strings.zoneNames.Get(progress_.zoneId);
    if (zoneName && zoneName[0] != '\0')
        L.line1 = Fmt("%s - Quete NPC #%d", zoneName, progress_.npcQuestId);
    else
        L.line1 = Fmt("Zone #%d - Quete NPC #%d", progress_.zoneId, progress_.npcQuestId);

    // --- Catégorie / type d'interaction (+72 du record, valeurs 1..6 non mappées
    //     à un libellé textuel connu dans le désassemblage disponible) ---
    L.line2 = Fmt("Categorie : %u", npc->category);

    // --- Objectif courant : cible + progression/requis (Quest_CheckObjectiveState
    //     compare ces mêmes champs en interne) ---
    L.line3 = Fmt("Objectif : cible #%u (%d/%u)",
                  npc->targetId, progress_.objectiveProgress, npc->required);

    // --- État (Quest_CheckObjectiveState 0x50FF10 / Quest_IsObjectiveComplete 0x5103F0) ---
    const bool complete = game::Quest_IsObjectiveComplete(progress_);
    const int  code     = game::Quest_CheckObjectiveState(progress_);
    if (complete) {
        L.line4 = "Etat : Termine";
        L.line4Color = kColSuccess;
    } else if (code == 0) {
        L.line4 = "Etat : Invalide";
        L.line4Color = kColError;
    } else {
        L.line4 = "Etat : En cours";
        L.line4Color = kColPending;
    }

    // --- Récompense potentielle (Quest_GetRewardItemId 0x510A10) ---
    // Quest_GetRewardItemId/Quest_IsRewardItemActive résolvent via
    // LookupQuestStep(zoneId, npcQuestId) (résolveur QuestStepLookup injectable,
    // cf. QuestSystem.h/.cpp) : tant qu'aucun chargeur NPC réel n'est branché
    // (TODO PRECIS du header), cette résolution renvoie 0/false même si
    // CurrentQuestStepRecord() est peuplé par ailleurs. On retente donc en lisant
    // directement le premier slot de récompense de type==6 (id d'item) du record
    // déjà en main, pour un affichage robuste sans dépendre du branchement futur.
    int rewardItemId = game::Quest_GetRewardItemId(progress_);
    if (rewardItemId == 0) {
        for (const auto& r : npc->reward) {
            if (r.type == 6) { rewardItemId = static_cast<int>(r.value); break; }
        }
    }
    L.rewardActive = game::Quest_IsRewardItemActive(progress_);

    if (rewardItemId > 0) {
        const game::ItemInfo* item = game::GetItemInfo(static_cast<uint32_t>(rewardItemId));
        if (item)
            L.line5 = Fmt("Recompense : %s (#%d)%s", item->name, rewardItemId,
                          L.rewardActive ? " [active]" : "");
        else
            L.line5 = Fmt("Recompense : objet #%d%s", rewardItemId,
                          L.rewardActive ? " [active]" : "");
    } else {
        L.line5 = "Recompense : aucune";
    }

    // --- Géométrie (ancré haut-droite, indépendant de la résolution écran) ---
    L.w = kPanelW;
    L.h = kPadY + kTitleH + 4 + 5 * kLineH + kPadY;
    L.x = screenW - kPanelW - kMarginX;
    L.y = kMarginY;
    (void)screenH;

    return L;
}

bool QuestTrackerWindow::OnMouseDown(int x, int y) {
    // Consomme uniquement si le clic tombe SUR le panneau actuellement dessiné
    // (évite le clic-traversant vers le monde 3D sous ce HUD). Pas de bouton :
    // aucune autre action.
    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

bool QuestTrackerWindow::OnClick(int x, int y) {
    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

void QuestTrackerWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    (void)cursorX; (void)cursorY;

    const Layout L = BuildLayout(ctx.screenW, ctx.screenH);

    // bOpen_ / x_ / y_ (champs protégés de Dialog) reflètent l'état auto-masqué :
    // recalculés à CHAQUE Render (les deux phases donnent le même résultat dans
    // la même frame), donc un Close() externe éventuel n'a pas d'effet durable —
    // conforme à « toujours visible si une quête est active ».
    bOpen_ = L.visible;
    x_ = L.x;
    y_ = L.y;

    // Mémorise la géométrie effectivement dessinée pour le hit-test (routé entre
    // deux frames), même motif que MsgBoxDialog::lastScreenW_/lastScreenH_.
    lastVisible_ = L.visible;
    lastX_ = L.x; lastY_ = L.y; lastW_ = L.w; lastH_ = L.h;

    if (!L.visible) return;

    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, L.x, L.y, L.w, L.h, kColBg);
        ctx.DrawFrame(L.x, L.y, L.w, L.h, kColBorder, 1);
        return;
    }

    // Phase texte.
    int ty = L.y + kPadY;
    const char* title = "Quete en cours";
    ctx.Text(title, L.x + (L.w - ctx.MeasureText(title)) / 2, ty, kColTitle);
    ty += kTitleH + 4;

    ctx.Text(L.line1.c_str(), L.x + kPadX, ty, kColText); ty += kLineH;
    ctx.Text(L.line2.c_str(), L.x + kPadX, ty, kColText); ty += kLineH;
    ctx.Text(L.line3.c_str(), L.x + kPadX, ty, kColText); ty += kLineH;
    ctx.Text(L.line4.c_str(), L.x + kPadX, ty, L.line4Color); ty += kLineH;
    ctx.Text(L.line5.c_str(), L.x + kPadX, ty, kColText); ty += kLineH;
}

} // namespace ts2::ui

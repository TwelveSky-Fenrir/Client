// UI/QuestTrackerWindow.cpp — implementation of the quest-tracking panel.
// See UI/QuestTrackerWindow.h for the contract and reservations on the
// displayed data (two distinct quest data sources, cf. Game/QuestSystem.h).
//
// NETWORK: this file includes neither Net/SendPackets.h nor Net/NetClient.h, and
// calls NO net::Net_Send* — that is INTENTIONAL and PROVEN, not an oversight. The
// panel's two original functions (Quest_DrawTracker 0x510FC0 = pure rendering;
// Quest_UpdateMarkerTimer 0x510D90 = state/sound only) emit nothing: see the detailed
// proof at the top of UI/QuestTrackerWindow.h. Quest progress arrives via inbound
// Pkt_* handlers, never via an action from this panel.
#include "UI/QuestTrackerWindow.h"
#include "UI/PanelSkin.h"
#include "Game/StringTables.h" // game::g_Strings.zoneNames (003.DAT -> mZONENAME)

#include <cstdarg>
#include <cstdio>

namespace ts2::ui {

namespace {
// Real panel background (best effort): narrow/tall (252,440) template, the MOST
// repeated (63 non-consecutive occurrences) in the UI atlas folder
// G03_GDATA/D01_GIMAGE2D/001 — NOT CONFIRMED by IDA, kept as the default for
// this narrow HUD panel (260 px wide, dynamic height based on displayed lines;
// see the methodology in UI/PanelSkin.h). Distinct index from the one used by
// PartyWindow. Falls back to kColBg automatically if missing.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00516.IMG");

// Formatting without exceptions or excessive dynamic allocation (snprintf ->
// std::string). `fmt`: classic C format string.
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

    // Visibility guard: CurrentQuestStepRecord() (mirrors g_pCurQuestStepRecord /
    // dword_18231B4). nullptr => no active quest => panel fully hidden.
    const game::QuestStepRecord* npc = game::CurrentQuestStepRecord();
    if (!npc) return L; // L.visible stays false

    L.visible = true;

    // --- Step identifier: REAL zone name if 003.DAT (mZONENAME) knows it for
    //     progress_.zoneId (StrTable003_Get 0x4C1AD0, 1-based index, cf.
    //     Game/StringTables.h); falls back to the numeric id if the table is empty/
    //     not loaded or the entry doesn't exist (Get() returns "" out of bounds). No
    //     quest name available (cf. .h banner: QuestTbl (A) and mQUEST (B) are two
    //     sources with no known join) -> npcQuestId stays numeric.
    const char* zoneName = game::g_Strings.zoneNames.Get(progress_.zoneId);
    if (zoneName && zoneName[0] != '\0')
        L.line1 = Fmt("%s - Quete NPC #%d", zoneName, progress_.npcQuestId);
    else
        L.line1 = Fmt("Zone #%d - Quete NPC #%d", progress_.zoneId, progress_.npcQuestId);

    // --- Category / interaction type (+72 of the record, values 1..6 not mapped
    //     to any known text label in the available disassembly) ---
    L.line2 = Fmt("Categorie : %u", npc->category);

    // --- Current objective: target + progress/required (Quest_CheckObjectiveState
    //     compares these same fields internally) ---
    L.line3 = Fmt("Objectif : cible #%u (%d/%u)",
                  npc->targetId, progress_.objectiveProgress, npc->required);

    // --- State (Quest_CheckObjectiveState 0x50FF10 / Quest_IsObjectiveComplete 0x5103F0) ---
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

    // --- Potential reward (Quest_GetRewardItemId 0x510A10) ---
    // Quest_GetRewardItemId/Quest_IsRewardItemActive resolve via
    // LookupQuestStep(zoneId, npcQuestId) (injectable QuestStepLookup resolver,
    // cf. QuestSystem.h/.cpp): until a real NPC loader is wired in (header's
    // PRECISE TODO), this resolution returns 0/false even if
    // CurrentQuestStepRecord() is populated otherwise. So we retry by reading
    // directly the first type==6 reward slot (item id) from the record already
    // in hand, for a robust display that doesn't depend on the future wiring.
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

    // --- Geometry (anchored top-right, independent of screen resolution) ---
    L.w = kPanelW;
    L.h = kPadY + kTitleH + 4 + 5 * kLineH + kPadY;
    L.x = screenW - kPanelW - kMarginX;
    L.y = kMarginY;
    (void)screenH;

    return L;
}

bool QuestTrackerWindow::OnMouseDown(int x, int y) {
    // Only consumes if the click lands ON the currently drawn panel
    // (avoids click-through to the 3D world under this HUD). No button:
    // no other action.
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

    // bOpen_ / x_ / y_ (Dialog's protected fields) reflect the auto-hidden state:
    // recalculated on EVERY Render (both phases give the same result within the
    // same frame), so an external Close() call has no lasting effect — consistent
    // with "always visible while a quest is active".
    bOpen_ = L.visible;
    x_ = L.x;
    y_ = L.y;

    // Stores the geometry actually drawn for hit-testing (routed across
    // two frames), same pattern as MsgBoxDialog::lastScreenW_/lastScreenH_.
    lastVisible_ = L.visible;
    lastX_ = L.x; lastY_ = L.y; lastW_ = L.w; lastH_ = L.h;

    if (!L.visible) return;

    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, L.x, L.y, L.w, L.h, kColBg);
        ctx.DrawFrame(L.x, L.y, L.w, L.h, kColBorder, 1);
        return;
    }

    // Text phase.
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

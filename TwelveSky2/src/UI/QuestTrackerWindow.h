// UI/QuestTrackerWindow.h — quest-tracking panel (ts2::ui).
//
// Compact HUD window, anchored top-right, ALWAYS DISPLAYED while a quest step
// is active client-side. Wired to Game/QuestSystem.h (already written):
//   - CurrentQuestStepRecord() / SetCurrentQuestStepRecord(): cached step record
//     (mirrors g_pCurQuestStepRecord / dword_18231B4). Serves as the visibility
//     GUARD (nullptr => panel hidden) and as the source of the objective's
//     numeric fields (targetId/required/category/reward[3]) — this record
//     CARRIES NO free-form name/text (cf. QuestSystem.h banner: the QuestTbl
//     table (A) which has names, and the NPC table mQUEST (B) this record
//     comes from, are TWO DISTINCT SOURCES with no known join between them).
//     Lacking a name/text, the panel displays the raw identifiers (zone/NPC
//     quest) — no invented text.
//   - Quest_CheckObjectiveState / Quest_IsObjectiveComplete / Quest_GetRewardItemId
//     : evaluated on a ts2::game::QuestProgressState LOCAL to this window (no
//     global instance is exposed by QuestSystem.h/ClientRuntime.h as of writing).
//     Exposed via Progress() for future wiring by the system that will maintain
//     the player's real state (zoneId, npcQuestId, objectiveMode/Type/Target/
//     Progress) — same injection pattern as QuestStepLookup in QuestSystem.h.
//
// PRECISE TODO (state, not network): wire Progress() to the future client mirror
// of offsets +10249/+10254/+11553..+11557 of g_PlayerCmdController (0x1669170),
// updated by the relevant Pkt_* handlers (quest progress, kill-track) — out of
// scope for this UI mission.
//
// NO NETWORK EMISSION — PROVEN, NOT ASSUMED (Pass 4 / wave W6, `quest-npcdialog` front).
// Quest tracking is PURELY LOCAL on the binary side. This panel's two original functions
// contain NO Net_Send* call at all (verified against their decompilation's reference
// list, not an approximate read):
//   Quest_DrawTracker      0x510FC0 — pure rendering: SkillDefTbl_GetRecord(mNPC, this+51588) ->
//     Sprite2D_Draw(&g_AssetMgr_UiAtlasSlots + 148*rec[1320] - 148); NpcTbl_FindByTypeAndId
//     (mQUEST, this+40996, this+46212 (+1 if this+51584==1)); 4x UI_DrawNumberValue at
//     y+3/+19/+35/+51; formats "%s (%d,%d)" (GInfo_CalcRightMargin/CalcLeftMargin/
//     FindMotionByFrameId + StrTable003_Get) and "%s!" (this+40980). Vertical anchor:
//     dword_184C648==1 ? this+24-352 : this+24-196.
//   Quest_UpdateMarkerTimer 0x510D90 — state + audio only: guard !Map_IsArenaZone()
//     (0x54B690), marker extinguished at 30 s, re-evaluated at 600 s, Quest_CheckObjectiveState
//     -> NpcTbl_FindByTypeAndId(mQUEST, ...); Snd3D_PlayScaledVolume(flt_148CABC,...,100,1) if
//     code==1, else this+51592 = Rng_Next()%3+1.
// => Emitting nothing from this panel is CORRECT and FAITHFUL. This is not a porting
// gap: adding a send here would be an INVENTION. (Old unanchored wording "No action
// from this panel sends a network packet (read-only)." replaced by the two EAs above,
// which demonstrate it.)
#pragma once
#include "UI/UIManager.h"
#include "Game/QuestSystem.h"
#include "Game/GameDatabase.h"

#include <string>

namespace ts2::ui {

// QuestTrackerWindow — non-modal Dialog with no close button: its visibility
// (bOpen_) is recalculated on EVERY Render() from game::CurrentQuestStepRecord()
// (auto-hidden when no quest is active), so an external Open()/Close() has no
// lasting effect — consistent with the "always visible while a quest is
// active" requirement.
class QuestTrackerWindow : public Dialog {
public:
    QuestTrackerWindow() = default;

    // Player progress state consumed by Quest_IsObjectiveComplete /
    // Quest_GetRewardItemId / Quest_CheckObjectiveState. The future player
    // state system writes here (zoneId, npcQuestId, objectiveMode/Type/
    // Target/Progress) on every relevant tick / packet.
    game::QuestProgressState&       Progress()       { return progress_; }
    const game::QuestProgressState& Progress() const { return progress_; }
    void SetProgressState(const game::QuestProgressState& s) { progress_ = s; }

    // Mouse events: only consumes a click landing ON the panel (avoids
    // click-through to the 3D world under this HUD, cf. UIManager's "first
    // consumer wins" rule); does nothing else (no button — pure info panel).
    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    // No keyboard input consumed by this panel.
    bool OnKey(int vk) override { (void)vk; return false; }

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Layout {
        bool visible = false;
        int  x = 0, y = 0, w = 0, h = 0;
        std::string line1;      // quest identifier (zone/npcQuestId)
        std::string line2;      // category / interaction type
        std::string line3;      // objective (target + progress/required)
        std::string line4;      // state (text)
        D3DCOLOR    line4Color = 0xFFFFFFFFu;
        std::string line5;      // potential reward
        bool        rewardActive = false;
    };

    // Recomputes geometry + text from CurrentQuestStepRecord() + progress_.
    // Called at the start of BOTH Render phases (Panels/Text) — no order
    // dependency, identical result within the same frame.
    Layout BuildLayout(int screenW, int screenH) const;

    game::QuestProgressState progress_{};

    // Last drawn geometry, stored for hit-testing (OnMouseDown/OnClick are
    // routed across two frames, i.e. AFTER the last Render).
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
    static constexpr D3DCOLOR kColPending = Argb(255, 255, 221, 102); // in progress (= title)
    static constexpr D3DCOLOR kColError   = Argb(255, 255, 96, 96);   // ~0xFFFF6060
};

} // namespace ts2::ui

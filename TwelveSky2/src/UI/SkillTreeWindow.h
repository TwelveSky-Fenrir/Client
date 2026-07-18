// UI/SkillTreeWindow.h — "Skill Tree" window of the TwelveSky2 client.
//
// Interactive view over game::SkillBar (Game/SkillSystem.h, already written): grid
// 8 columns x 5 rows = 40 slots, mapped EXACTLY onto
// SkillBar::slots (40 slots {skillId, spCost}, see g_LearnedSkills comment
// 0x16742BC). On the right, a REAL node grid (4x3, paginated) scanning the ENTIRE
// known skill tree (skillId 1..kMaxSkillId present in skillTbl,
// see game::SkillLevelTable) — not just immediately learnable skills:
// each node shows its icon (.IMG best-effort, falls back to a colored
// dot, see NoteSkillIcon in the .cpp), its required level, and its state
// (Locked = level/branch not reached, Available = learnable now,
// Learned = already in the bar), see NodeState/NodeStateOf. Interaction:
//   1. clicking an Available node in the "Available" grid -> selects the
//      candidate skill (highlighted); clicking Locked/Learned -> status
//      message only (no selection);
//   2. clicking an EMPTY bar slot with a skill
//      selected -> attempts to learn it (AttemptLearn): checks
//      self.skillPoints >= SP cost (SKILL_INFO +0x230, skillinfo::kOffSpCost,
//      real guard UI_SkillLearn_OnLDown 0x5E1DC4) then... APPLIES NOTHING.
//      Fixed Pass 4 / W6: this step used to debit self.skillPoints and place
//      the skill in the bar via game::Skill_Learn — an OPTIMISTIC local effect
//      the binary does NOT perform (the real confirmation,
//      UI_MsgBox_OnLButtonUp case 3 @0x5C0C23, only EMITS and waits for the
//      server), and moreover borrowed from a different flow
//      (Skill_Learn is anchored on the INBOUND handler Pkt_ItemAction G0
//      0x46A456 = learning via a BOOK item). The emission that should
//      replace it (opcode 0x13 / sub-code 202) is BLOCKED: it requires the
//      trainer NPC's id, which this window does not have — see the TODO [ancre
//      0x5C0C5E] detailed above AttemptLearn() in the .cpp.
// Hovering a LEARNED slot: tooltip with the SP cost actually debited
// at learn time (memoized in the slot) AND the nominal MP cast cost
// (Skill_CostById 0x4CD0E0). Header bar: unspent skill points
// (self.skillPoints) + current active posture/stance
// (Skill_GetActiveStance 0x4FB210, Game/SkillCombat.h).
//
// No action in this window sends a network packet — AUDITED state (Pass 4 /
// W6), for two DIFFERENT reasons, not to be confused:
//   - Selecting a node: MUST NOT EMIT ANYTHING. Proven: UI_SkillLearn_OnLDown
//     0x5E1C40 emits nothing, it validates (SP / already learned / free slot) then opens a
//     confirmation box (UI_MsgBox_Open kind 3, 0x5E20C0). Network silence on
//     click is therefore FAITHFUL, not a gap.
//   - Confirming the learn: SHOULD emit (opcode 0x13, sub-code 202,
//     [npcId:i32][skillId:i32]) but remains BLOCKED for lack of a proven npcId. UPDATE
//     (Pass 4 / W6, IDA re-check): the builder Net_SendVaultReq_202 has since been corrected
//     to (NetClient&, int32_t npcId, int32_t skillId) (Net/SendPackets.h:242) — it no
//     longer truncates anything and matches the wire. The ONLY remaining blocker is the
//     npcId, which this window (generic tree, 'K' hotkey) does not have. Full
//     detail + layout in the TODO [ancre 0x5C0C5E] above AttemptLearn() (.cpp).
// Learning is also no longer applied to local state (the binary doesn't do it
// either): the window therefore remains a VIEW of the tree until the NPC flow is ported.
//
// LAYOUT — CONFIRME_FIDELE (2026-07-14, idaTs2 decompilation, re-verified
// the same day): the original binary has no tree layout with custom
// per-node positions nor parent-child connector lines — its
// "real" layout (UI_SkillLearn_Draw 0x5E2200, 3 columns x 8
// rows per trainer NPC, fixed pixel formula) IS a simple grid;
// this fact is confirmed, not a research limitation. This window shows
// a different GENERIC paginated grid (not the per-NPC 3x8 grid) by
// deliberate architecture choice (unported NPC->skills backend +
// different opening contract), documented precisely in the note block
// at the top of the .cpp and the "TREE LAYOUT —
// CONFIRME_FIDELE" block at the top of Game/SkillSystem.h. On the other hand the name,
// icon index, and prerequisites shown per node are now read
// from the REAL SKILL_INFO fields (Skill_GetName, Skill_GetIconIndex,
// kOffReqWeapon/kOffReqBranch/kOffSection).
//
// Project rule: this file does not edit ANY existing header; it only
// includes UI/UIManager.h, Game/SkillSystem.h and Game/SkillCombat.h read-only.
#pragma once
#include "UI/UIManager.h"
#include "Game/SkillSystem.h"
#include "Game/SkillCombat.h"
#include "Gfx/GpuTexture.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace ts2::ui {

// SkillTreeWindow — modal Dialog (while open) displaying/driving the learned
// skill bar. Inherits from ts2::ui::Dialog (UIManager contract, not
// edited). Requires a Bind() before any useful render (no crash if not bound
// : the window renders empty and silently refuses actions).
class SkillTreeWindow : public Dialog {
public:
    SkillTreeWindow();

    // Binds the window to the required runtime data: SKILL_INFO table (for
    // Skill_GetRecord/SP cost), ITEM_INFO table (for Skill_CostById), level
    // bound table (Skill_IsAvailableByLevel), the learned skill bar,
    // and the local player state (skillPoints/level/rebirth, READ
    // only). `morph` is optional (posture/rebirth) — see
    // Game/SkillCombat.h.
    // NOTE (Pass 4 / W6): `bar` and `self` are NEVER written by this
    // window anymore — local learning (game::Skill_Learn) has been removed, the binary
    // waits for the server (see AttemptLearn in the .cpp). The references remain
    // non-const because Bind()'s contract is shared/called by UI/GameWindows.cpp
    // (file not owned by this front): hardening it to const is out of scope.
    //
    // NO npcId: this is precisely what blocks emission of sub-code 202
    // (see TODO [ancre 0x5C0C5E] above AttemptLearn in the .cpp). The real
    // learning widget is opened BY a trainer NPC and holds its
    // record (*(this+2) == dword_1822ED0); this window is opened via the
    // 'K' hotkey and has none.
    void Bind(const game::DataTable& skillTbl, const game::DataTable& itemTbl,
              const game::SkillLevelTable& lvlTbl, game::SkillBar& bar,
              game::SelfState& self, const game::CombatMorphState& morph = {});

    void Open() override;  // Dialog::Open() + resets selection/page/status
    void Close() override { Dialog::Close(); }

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    // Currently "armed" target (button/slot pressed down, awaiting
    // release on top of it) — same pattern as OptionsWindow/WarehouseWindow.
    enum class Target { None, Close, PrevPage, NextPage, GridSlot, CandidateRow };

    // State of a skill-tree node (the "Available" list, now
    // a REAL node grid — not just immediately learnable skills):
    // Locked = level/branch not reached (Skill_IsAvailableByLevel
    // false), Available = learnable now (selectable), Learned = already
    // present in bar_ (shown for the tree overview, not selectable).
    enum class NodeState { Locked, Available, Learned };
    NodeState NodeStateOf(int skillId) const;

    // Geometry recomputed every frame from screen dimensions
    // (centering); hit-testing (routed between two frames) relies on
    // lastScreenW_/lastScreenH_ memoized from the last Render.
    void Layout(int screenW, int screenH, Rect& panel, Rect& close, Rect& grid,
                Rect& candPanel, Rect& prevBtn, Rect& nextBtn) const;

    Rect SlotRect(const Rect& grid, int slotIndex) const;
    // Cell of the "Available" node grid, flat-indexed (row-major,
    // kCandCols columns) — replaces the old list of text lines.
    Rect CandidateCellRect(const Rect& candPanel, int cellIndex) const;

    // Icon for a node (lazy, cached): see NoteSkillIcon in the
    // .cpp for the methodology (automatic fallback to a colored dot).
    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t skillId);

    // Rebuilds the list of learnable skills (candidates_) from
    // skillTbl_/lvlTbl_/bar_/self_. Called at the top of Render (both
    // phases read the same result, computed once per frame).
    void RecomputeCandidates();

    void ActivateIfHit(const Rect& close, const Rect& grid, const Rect& candPanel,
                        const Rect& prevBtn, const Rect& nextBtn, int x, int y);
    void HandleGridSlotClick(int slotIndex);
    void HandleCandidateClick(int rowOnPage);
    // Learn confirmation — mirrors UI_MsgBox_OnLButtonUp case 3 (0x5C0C23).
    // Does NOT emit (npcId unavailable) and applies NO local effect, like the binary:
    // see the TODO [ancre 0x5C0C5E] detailed above the definition in the .cpp.
    void AttemptLearn();

    static bool In(const Rect& r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    // --- Data binding (not owned) ---
    const game::DataTable*       skillTbl_ = nullptr;
    const game::DataTable*       itemTbl_  = nullptr;
    const game::SkillLevelTable* lvlTbl_   = nullptr;
    game::SkillBar*              bar_      = nullptr;
    game::SelfState*             self_     = nullptr;
    game::CombatMorphState       morph_{};
    bool                         bound_    = false;

    // --- Interaction state ---
    Target      armedTarget_    = Target::None;
    int         armedGridSlot_  = -1;
    int         armedCandRow_   = -1;
    uint32_t    selectedSkillId_ = 0; // selected candidate skill (right-hand list)
    int         page_           = 0; // current page of the "Available" list
    std::vector<int> candidates_;    // learnable skillIds, recomputed every frame

    std::string statusText_;         // last action result, shown in the window footer
    bool        statusIsError_ = false;

    // Node icons, lazy + cached (same pattern as
    // InventoryWindow::iconCache_ / WarehouseWindow::iconCache_). Cleared implicitly at
    // shutdown (lifetime = process, like the other windows).
    std::unordered_map<uint32_t, gfx::GpuTexture> iconCache_;

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Geometry (panel coordinates, 1024x768 reference) ---
    static constexpr int kCols        = 8;
    static constexpr int kRows        = 5;   // 8x5 = 40 == SkillBar::slots.size()
    static constexpr int kCellSize    = 40;
    static constexpr int kCellGap     = 4;
    static constexpr int kCellPitch   = kCellSize + kCellGap;
    static constexpr int kGridW       = kCols * kCellPitch - kCellGap;
    static constexpr int kGridH       = kRows * kCellPitch - kCellGap;

    static constexpr int kGridPad     = 14;
    static constexpr int kTitleH      = 26;
    static constexpr int kHeaderInfoH = 20;
    static constexpr int kInfoBarH    = 32; // hover tooltip (2 lines) between grid and footer
    static constexpr int kFooterH     = 40;
    static constexpr int kCloseBtn    = 18;

    static constexpr int kCandPanelW  = 200;
    static constexpr int kCandHeaderH = 16;
    static constexpr int kCandBtnH    = 16;
    // "Available" node grid: same kCellSize/kCellGap/kCellPitch as
    // the SkillBar grid (uniform icon size across the whole window).
    static constexpr int kCandCols     = 4;
    static constexpr int kCandRows     = 3;
    static constexpr int kItemsPerPage = kCandCols * kCandRows; // 12 nodes/page

    static constexpr int kPanelW = kGridPad * 2 + kGridW + kGridPad + kCandPanelW;
    static constexpr int kPanelH = kTitleH + kHeaderInfoH + kGridH + kInfoBarH + kFooterH;

    // Upper bound of known skill ids (SkillLevelTable, see SkillSystem.h: skillId 1..350).
    // Also used as the bound for icon resolution (atlas UI folder 003, 755
    // CONTIGUOUS files 1..755 — well beyond, see NoteSkillIcon in the .cpp).
    static constexpr int kMaxSkillId = 350;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, per UI contract) ---
    static constexpr D3DCOLOR kColBg            = 0xE0202028u; // panel background
    static constexpr D3DCOLOR kColBorder        = 0xFF808080u; // frame
    static constexpr D3DCOLOR kColTitleBg       = 0xFF2C2C3Cu; // title bar
    static constexpr D3DCOLOR kColTitle         = 0xFFFFDD66u; // title
    static constexpr D3DCOLOR kColText          = 0xFFFFFFFFu; // text
    static constexpr D3DCOLOR kColTextDim       = 0xFFAAAAAAu; // dimmed text
    static constexpr D3DCOLOR kColSelect        = 0xFF4060A0u; // hover/selection
    static constexpr D3DCOLOR kColError         = 0xFFFF6060u; // error
    static constexpr D3DCOLOR kColSuccess       = 0xFF60FF60u; // success
    static constexpr D3DCOLOR kColClose         = 0xFFB03A3Au;
    static constexpr D3DCOLOR kColBtnHover      = 0xFF4060A0u;
    static constexpr D3DCOLOR kColBtn           = 0xFF38384Au;
    static constexpr D3DCOLOR kColBtnOff        = 0xFF26262Au;
    static constexpr D3DCOLOR kColCellLearned      = 0xFF34503Eu; // learned slot (muted green)
    static constexpr D3DCOLOR kColCellLearnedHover = 0xFF3C6048u;
    static constexpr D3DCOLOR kColCellEmpty        = 0xFF1A1A20u; // empty slot
    static constexpr D3DCOLOR kColCellEmptyHover   = 0xFF2A2A34u;
    static constexpr D3DCOLOR kColRowBg         = 0xFF262630u; // "Available" list cell background
    static constexpr D3DCOLOR kColRowHover      = 0xFF32324Au;

    // Fallback dots by node state (.IMG icon unavailable/not loaded) —
    // see NodeState / NoteSkillIcon (.cpp).
    static constexpr D3DCOLOR kColPastilleLocked    = 0xFF4A4A50u; // dull gray: locked
    static constexpr D3DCOLOR kColPastilleAvailable = 0xFFC9A227u; // gold: learnable
    static constexpr D3DCOLOR kColPastilleLearned   = 0xFF3FAE55u; // green: already learned
    static constexpr D3DCOLOR kColNodeBorderLocked    = 0xFF3A3A3Eu;
    static constexpr D3DCOLOR kColNodeBorderAvailable = 0xFFDDBB55u;
    static constexpr D3DCOLOR kColNodeBorderLearned   = 0xFF60FF60u;
    static constexpr D3DCOLOR kColNodeDimOverlay      = 0x90101014u; // dims a locked node's icon
};

} // namespace ts2::ui

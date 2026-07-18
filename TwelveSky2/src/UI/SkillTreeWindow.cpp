// UI/SkillTreeWindow.cpp — implementation of the Skill Tree window.
// See SkillTreeWindow.h for the full interaction contract.
//
// LAYOUT — CONFIRME_FIDELE (2026-07-14, idaTs2 decompilation, re-verified
// the same day via a second pass + xrefs_to on SkillGrowthTbl_GetRecord
// 0x4C4E90: only UI_SkillLearn_OnLDown/Draw/OnMove read this table on the
// UI side, no other "tree" widget exists in the binary). See the detailed
// note block at the top of Game/SkillSystem.h for exact addresses and
// re-verification details. Verdict summary:
//   - CONFIRME_FIDELE: the original binary has NO tree layout with custom
//     per-node positions or parent-child connector lines. The real learning
//     widget (UI_SkillLearn_Draw 0x5E2200 and sibling functions
//     0x5E1BA0..0x5E2450) is a simple FIXED grid, pixel FORMULA
//     (x = x0+76*i+35, y = y0+54*j+71), 3 columns (weapon branch) x 8 rows
//     (tier), SPECIFIC TO EACH trainer NPC opened (each cell's skillId comes
//     from the NPC itself, not a global table) — and it draws NO parent-child
//     connector line (verified by reading the full function body, twice).
//     This is no longer a research limitation: the "simple grid" layout IS
//     the true original-game layout, confirmed by decompilation.
//   - What remains a genuine architecture choice (distinct from the point
//     above, NOT an unrecovered layout): this window (SkillTreeWindow)
//     deliberately stays a GENERIC paginated grid (kCandCols x kCandRows,
//     see .h) instead of reproducing the per-NPC 3x8 grid verbatim, for two
//     precise reasons:
//       1) Missing backend: the "which skillId in which cell, for which NPC"
//          table has no equivalent in GameDatabases/DataTable on the
//          rewritten client side (no NPC->taught-skills table ported);
//          without it, a "true layout" 3x8 grid would show empty cells or
//          invented data — less faithful than the current paginated list,
//          which scans the REAL SKILL_INFO table in full.
//       2) Different opening contract: in the original, this window opens by
//          interacting with a trainer NPC (Enter/Open mode, faction/element
//          gate); here it opens via the global 'K' hotkey (GameWindows.h) —
//          rearchitecting this contract is out of scope for this pass
//          (does not touch Scene/SceneManager).
//   - What IS now faithful (previously approximate): the name shown per node
//     comes from the record's real field (game::Skill_GetName, skillinfo::
//     kOffName) instead of a plain "#id"; the probed icon index comes from
//     the record's real skillinfo::kOffIconIndex field (idx137) instead of
//     an "index == skillId" assumption (which was WRONG, cf. NoteSkillIcon);
//     the detail panel also shows real prerequisites (weapon/branch/section,
//     SKILL_INFO fields) in addition to level and SP cost.
#include "UI/SkillTreeWindow.h"
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"
// Locks checked by the learn confirmation (UI_MsgBox_OnLButtonUp case 3,
// 0x5C0C23/0x5C0C36): net::g_MorphInProgress 0x1675A88, net::g_GmCmdCooldownLatch
// 0x1675B08. See AttemptLearn(). The 202 emission itself remains NOT wired (npcId
// unavailable): no Net/SendPackets.h include here, that would be a dead include.
#include "Net/ClientState.h"

#include <cstdio> // snprintf

namespace ts2::ui {

namespace {
// Real panel background (best effort): wide template (702,488) from UI atlas
// folder G03_GDATA/D01_GIMAGE2D/001 — candidate NOT CONFIRMED by IDA, chosen by
// ratio proximity to the Skill Tree panel (~590x334, wide panel; see detailed
// methodology in UI/PanelSkin.h). Automatic fallback to kColBg if absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01672.IMG");

// NoteSkillIcon — .IMG path for a skill node's icon.
//
// INDEX SOURCE (updated 2026-07-14, IDA accessible again): CONFIRMED by direct
// decompilation that a node's icon is NOT indexed by skillId but by a dedicated
// field of the SKILL_INFO record — game::skillinfo::kOffIconIndex (idx137,
// +0x224), read via Skill_GetIconIndex(rec). See UI_SkillLearn_Draw 0x5E2200
// (body: Sprite2D_Draw((int)&unk_A1BD60 + 148*v16, ...) with v16 = v12[137]-1):
// the earlier "index = skillId" assumption below was therefore an approximation,
// now corrected.
//
// DESTINATION (.IMG folder): however, the original atlas (unk_A1BD60, a
// 148-byte/entry table pointing to preloaded Sprite2D from a file not
// identified in this session — no .npk/.IMG resolved by direct xref) has NO
// "one file per icon" equivalent recovered on the rewritten client side. We
// keep the SAME fallback METHODOLOGY as ResolveItemIconPath
// (UI/InventoryWindow.cpp): direct probing of the .IMG files in folder
// G03_GDATA/D01_GIMAGE2D/003 (755 files, CONTIGUOUS index 1..755, 25/50 px),
// but now addressed by the REAL icon index (kOffIconIndex) instead of skillId
// — NOT CONFIRMED that this folder is the right destination (no direct xref
// between kOffIconIndex and a G03_GDATA path), but the INDEX used to probe
// this folder is now real binary data rather than a guess. AUTOMATIC and
// SILENT fallback to a colored dot (see NodeState) on ALL failure paths: file
// missing, D3D9 device unavailable, GPU decode/texture failure.
std::string ResolveSkillIconPath(uint32_t iconIndex) {
    if (iconIndex == 0 || iconIndex > 755) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\003\\003_%05u.IMG", iconIndex);
    return std::string(buf);
}

// Label shown for a node: real name (game::Skill_GetName, see skillinfo::
// kOffName) if the record exists, else fallback "#id". The EXACT width of the
// name field in the record is not confirmed (only the trailing NUL bounds it):
// defensively truncated via format precision ("%.*s", maxChars) so a
// mis-terminated field never overflows into the rest of the UI; maxChars is
// set by the caller based on available width (40px cell vs tooltip).
void FormatSkillLabel(char* out, size_t outSize, const uint8_t* rec, uint32_t skillId, int maxChars) {
    const char* name = rec ? game::Skill_GetName(rec) : "";
    if (name && name[0] != '\0')
        std::snprintf(out, outSize, "%.*s", maxChars, name);
    else
        std::snprintf(out, outSize, "#%u", skillId);
}
} // namespace

SkillTreeWindow::SkillTreeWindow() {
    x_ = 0; y_ = 0; bOpen_ = false;
}

void SkillTreeWindow::Bind(const game::DataTable& skillTbl, const game::DataTable& itemTbl,
                            const game::SkillLevelTable& lvlTbl, game::SkillBar& bar,
                            game::SelfState& self, const game::CombatMorphState& morph) {
    skillTbl_ = &skillTbl;
    itemTbl_  = &itemTbl;
    lvlTbl_   = &lvlTbl;
    bar_      = &bar;
    self_     = &self;
    morph_    = morph;
    bound_    = true;
}

void SkillTreeWindow::Open() {
    Dialog::Open();
    page_ = 0;
    selectedSkillId_ = 0;
    statusText_.clear();
    statusIsError_ = false;
    armedTarget_ = Target::None;
    armedGridSlot_ = -1;
    armedCandRow_ = -1;
}

void SkillTreeWindow::Layout(int screenW, int screenH, Rect& panel, Rect& close, Rect& grid,
                              Rect& candPanel, Rect& prevBtn, Rect& nextBtn) const {
    panel.x = screenW / 2 - kPanelW / 2;
    panel.y = screenH / 2 - kPanelH / 2;
    panel.w = kPanelW;
    panel.h = kPanelH;

    // Close button (X) top-right of the panel.
    close = { panel.x + panel.w - kCloseBtn - 6, panel.y + 4, kCloseBtn, kCloseBtn };

    // 8x5 grid (= SkillBar::slots), below the title bar + header line.
    grid.x = panel.x + kGridPad;
    grid.y = panel.y + kTitleH + kHeaderInfoH;
    grid.w = kGridW;
    grid.h = kGridH;

    // List of learnable skills, right of the grid, same height.
    candPanel.x = grid.x + grid.w + kGridPad;
    candPanel.y = grid.y;
    candPanel.w = kCandPanelW;
    candPanel.h = kGridH;

    // Pagination buttons, anchored at the bottom of the candidates panel.
    const int bw = (kCandPanelW - 6) / 2;
    prevBtn = { candPanel.x,          candPanel.y + candPanel.h - kCandBtnH - 2, bw, kCandBtnH };
    nextBtn = { candPanel.x + bw + 6, candPanel.y + candPanel.h - kCandBtnH - 2, bw, kCandBtnH };
}

SkillTreeWindow::Rect SkillTreeWindow::SlotRect(const Rect& grid, int slotIndex) const {
    const int row = slotIndex / kCols;
    const int col = slotIndex % kCols;
    return { grid.x + col * kCellPitch, grid.y + row * kCellPitch, kCellSize, kCellSize };
}

SkillTreeWindow::Rect SkillTreeWindow::CandidateCellRect(const Rect& candPanel, int cellIndex) const {
    const int row = cellIndex / kCandCols;
    const int col = cellIndex % kCandCols;
    // Grid horizontally centered in candPanel (same pitch as the SkillBar grid).
    const int gridW = kCandCols * kCellPitch - kCellGap;
    const int originX = candPanel.x + (candPanel.w - gridW) / 2;
    const int originY = candPanel.y + kCandHeaderH;
    return { originX + col * kCellPitch, originY + row * kCellPitch, kCellSize, kCellSize };
}

SkillTreeWindow::NodeState SkillTreeWindow::NodeStateOf(int skillId) const {
    if (!bound_) return NodeState::Locked;
    for (const auto& s : bar_->slots) {
        if (s.skillId == static_cast<uint32_t>(skillId)) return NodeState::Learned;
    }
    if (game::Skill_IsAvailableByLevel(*lvlTbl_, skillId, self_->level, self_->levelBonus,
                                        morph_.rebirthTier)) {
        return NodeState::Available;
    }
    return NodeState::Locked;
}

gfx::GpuTexture* SkillTreeWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t skillId) {
    // Cache always keyed by skillId (stable, 1:1 with the displayed node) even
    // though file resolution now uses the record's real icon index
    // (skillinfo::kOffIconIndex) — see NoteSkillIcon above.
    auto it = iconCache_.find(skillId);
    if (it != iconCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    if (dev && bound_) {
        const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, static_cast<int>(skillId));
        const int iconIndex = rec ? game::Skill_GetIconIndex(rec) : 0;
        const std::string path = ResolveSkillIconPath(static_cast<uint32_t>(iconIndex));
        if (!path.empty()) {
            asset::ImgFile img;
            if (img.Load(path))
                tex.CreateFromImgFile(dev, img);
        }
    }
    // Also caches failure (invalid texture) to avoid retrying the load every
    // frame (same pattern as InventoryWindow::GetIconTex / WarehouseWindow::GetIconTex).
    auto res = iconCache_.emplace(skillId, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

// Rebuilds the tree's node list: ALL skillIds with a valid SKILL_INFO record
// (not just currently learnable ones), so the "Available" grid shows the
// OVERVIEW of the whole tree (locked/available/learned). Per-node state is
// recomputed on the fly by NodeStateOf (not memoized here): the list itself
// only changes if skillTbl_/kMaxSkillId change, but state depends on
// bar_/self_/morph_, which change every frame.
void SkillTreeWindow::RecomputeCandidates() {
    candidates_.clear();
    if (!bound_) { page_ = 0; return; }

    for (int id = 1; id <= kMaxSkillId; ++id) {
        if (!game::Skill_GetRecord(*skillTbl_, id)) continue; // record missing/empty
        candidates_.push_back(id);
    }

    const int maxPage = candidates_.empty() ? 0
        : (static_cast<int>(candidates_.size()) - 1) / kItemsPerPage;
    if (page_ > maxPage) page_ = maxPage;
    if (page_ < 0) page_ = 0;
}

bool SkillTreeWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Rect panel, close, grid, candPanel, prevBtn, nextBtn;
    Layout(lastScreenW_, lastScreenH_, panel, close, grid, candPanel, prevBtn, nextBtn);

    armedTarget_ = Target::None;
    armedGridSlot_ = -1;
    armedCandRow_ = -1;

    if (!In(panel, x, y)) return false; // click outside window: does not consume

    if (In(close, x, y)) {
        armedTarget_ = Target::Close;
    } else if (In(prevBtn, x, y)) {
        armedTarget_ = Target::PrevPage;
    } else if (In(nextBtn, x, y)) {
        armedTarget_ = Target::NextPage;
    } else if (In(grid, x, y)) {
        for (int i = 0; i < kCols * kRows; ++i) {
            if (In(SlotRect(grid, i), x, y)) { armedTarget_ = Target::GridSlot; armedGridSlot_ = i; break; }
        }
    } else if (In(candPanel, x, y)) {
        for (int i = 0; i < kItemsPerPage; ++i) {
            if (In(CandidateCellRect(candPanel, i), x, y)) {
                armedTarget_ = Target::CandidateRow; armedCandRow_ = i; break;
            }
        }
    }
    return true; // de facto modal panel while open: the whole surface consumes the click
}

void SkillTreeWindow::ActivateIfHit(const Rect& close, const Rect& grid, const Rect& candPanel,
                                     const Rect& prevBtn, const Rect& nextBtn, int x, int y) {
    switch (armedTarget_) {
        case Target::Close:
            if (In(close, x, y)) Close();
            break;
        case Target::PrevPage:
            if (In(prevBtn, x, y) && page_ > 0) --page_;
            break;
        case Target::NextPage: {
            const int maxPage = candidates_.empty() ? 0
                : (static_cast<int>(candidates_.size()) - 1) / kItemsPerPage;
            if (In(nextBtn, x, y) && page_ < maxPage) ++page_;
            break;
        }
        case Target::GridSlot: {
            if (armedGridSlot_ < 0 || armedGridSlot_ >= kCols * kRows) break;
            if (!In(SlotRect(grid, armedGridSlot_), x, y)) break;
            HandleGridSlotClick(armedGridSlot_);
            break;
        }
        case Target::CandidateRow: {
            if (armedCandRow_ < 0 || armedCandRow_ >= kItemsPerPage) break;
            if (!In(CandidateCellRect(candPanel, armedCandRow_), x, y)) break;
            HandleCandidateClick(armedCandRow_);
            break;
        }
        default: break;
    }
    armedTarget_ = Target::None;
    armedGridSlot_ = -1;
    armedCandRow_ = -1;
}

void SkillTreeWindow::HandleGridSlotClick(int slotIndex) {
    if (!bound_) return;
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= bar_->slots.size()) return;
    if (bar_->slots[static_cast<size_t>(slotIndex)].skillId != 0) {
        // Slot already occupied: no removal/reordering in this window.
        return;
    }
    if (selectedSkillId_ == 0) {
        statusText_ = "Sélectionnez d'abord une compétence dans la liste « Disponibles ».";
        statusIsError_ = true;
        return;
    }
    AttemptLearn();
}

void SkillTreeWindow::HandleCandidateClick(int rowOnPage) {
    const size_t idx = static_cast<size_t>(page_) * kItemsPerPage + static_cast<size_t>(rowOnPage);
    if (idx >= candidates_.size()) return;
    const int id = candidates_[idx];

    switch (NodeStateOf(id)) {
        case NodeState::Learned:
            statusText_ = "Compétence déjà apprise.";
            statusIsError_ = false;
            return; // no selection: nothing to learn
        case NodeState::Locked: {
            char buf[96];
            const int minLvl = lvlTbl_ ? lvlTbl_->Min(id) : 0;
            std::snprintf(buf, sizeof(buf), "Verrouillé : niveau %d requis.", minLvl);
            statusText_ = buf;
            statusIsError_ = true;
            return; // no selection: node not learnable
        }
        case NodeState::Available:
            break; // selectable, see below
    }

    selectedSkillId_ = static_cast<uint32_t>(id);
    statusText_ = "Compétence sélectionnée : cliquez un emplacement libre de la grille.";
    statusIsError_ = false;
}

// AttemptLearn — confirmation of learning a skill.
//
// REAL ANCHOR: UI_MsgBox_OnLButtonUp 0x5C0A90, case 3 (loc_5C0C23, via jump
// table jpt_5C0BE5) — this is the "OK" button of the confirmation box opened by
// UI_SkillLearn_OnLDown 0x5E1C40 (UI_MsgBox_Open(..., /*kind*/3, ...) 0x5E20C0).
// What the binary does, EXACTLY (disassembly re-read this pass):
//     cmp g_MorphInProgress, 1   -> if ==1: return 1              // 0x5C0C23 SILENT refusal
//     cmp g_GmCmdCooldownLatch,0 -> if !=0: return 1              // 0x5C0C36 SILENT refusal
//     mov ecx, dword_18231D0                                     // 0x5C0C49 skillId  -> a2
//     mov edx, dword_1822ED0 ; mov eax, [edx]                    // 0x5C0C50 npcId    -> a1
//     call Net_SendVaultReq_202                                  // 0x5C0C5E
//     mov g_GmCmdCooldownLatch, 1                                // 0x5C0C63
//     fld g_GameTimeSec ; fstp flt_1675B0C                       // 0x5C0C6D..0x5C0C73
//     mov dword_18231D0, 0                                       // 0x5C0C79 clears selection
// -> NO local effect: case 3 writes NEITHER g_LearnedSkills NOR g_SkillPointPool. The
//    binary WAITS for the server response (which also resets the latch to 0).
//
// FIDELITY FIX (Pass 4 / W6): the game::Skill_Learn(...) call that used to be
// here has been REMOVED. It debited self_->skillPoints AND placed the skill in
// the bar locally — an optimistic effect the binary does NOT perform (same
// defect noted on EnchantWindow in Pass 3). Also, Skill_Learn is anchored on
// Pkt_ItemAction G0 0x46A456 (see Game/SkillSystem.h:23), an INBOUND handler:
// that is learning via a skill BOOK item, a COMPLETELY DIFFERENT FLOW, wrongly
// reused here as a local UI action.
//
// TODO [ancre 0x5C0C5E] emission NOT wired — a SINGLE REMAINING blocker (the
// npcId, point 1), not bypassable without inventing data (rule "never guess");
// point 2 (the builder) has been RESOLVED since extraction and is kept only
// for the record:
//   1) npcId UNAVAILABLE. payload[0..3] = *dword_1822ED0 = the trainer NPC id
//      whose 3x8 grid is open (the real widget is PER-NPC). This window is a
//      GENERIC paginated tree opened via the 'K' hotkey (UI/GameWindows.cpp:129,
//      hotkeys::kSkillTree); Bind() carries NO NPC binding and no proven npcId
//      exists here. Emitting npcId=0 — or hijacking the vendor's NPC var
//      (dword_1837E6C, see UI/VendorShopWindow.cpp:329, a different global, a
//      different flow) — would be an invention. The real fix is porting the NPC
//      window (UI_SkillLearn_* 0x5E1BA0..0x5E2450 + MsgBox kind 3), out of scope
//      for this front.
//   2) builder — RESOLVED since extraction (no longer blocking). Net_SendVaultReq_202
//      was corrected to the wire-faithful signature:
//        void Net_SendVaultReq_202(NetClient& nc, int32_t npcId, int32_t skillId);
//      (Net/SendPackets.h:242 / SendPackets.cpp:2320 — copies f[2]={npcId,skillId}
//      over 8 bytes). Matches the binary (re-verified this pass): Net_SendVaultReq_202
//      0x590280 does Crt_Memcpy(v3, &a1, 4u) @0x5902A4 then Crt_Memcpy(v4, &a2, 4u)
//      @0x5902B6 (v4 contiguous with v3), i.e. payload[0..3]=npcId i32 LE,
//      payload[4..7]=skillId i32 LE (the `char a2` in the IDA prototype was an
//      inference, NOT the real wire format). The builder is therefore no longer a
//      blocker; only the npcId (point 1) is. The call is STILL NOT wired: an
//      invented npcId (0, or another flow's NPC var) would violate the "never
//      guess" rule, and a call guarded by an always-false `if (npcId != 0)` would
//      be DEAD CODE (a proven pitfall). The emission thus still needs to be ported
//      via the NPC flow (UI_SkillLearn_* + MsgBox kind 3), out of scope for this
//      front.
void SkillTreeWindow::AttemptLearn() {
    if (!bound_) return;

    const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, static_cast<int>(selectedSkillId_));
    if (!rec) {
        statusText_ = "Compétence introuvable dans la table.";
        statusIsError_ = true;
        selectedSkillId_ = 0;
        return;
    }

    // SP guard — UI_SkillLearn_OnLDown 0x5E1DC4: g_SkillPointPool >= Record[140]
    // (offset 560 = 0x230 == game::skillinfo::kOffSpCost), else message
    // StrTable005_Get(133+2=135) (0x5E1DD6). In the binary this check happens at
    // NODE SELECTION (before the confirmation box opens), not at confirmation; it
    // is kept here — where this window stands in for confirmation — so the guard
    // isn't lost.
    const int spCost = game::Skill_ReadI32(rec, game::skillinfo::kOffSpCost);
    if (self_->skillPoints < spCost) {
        statusText_ = "Points de compétence insuffisants.";
        statusIsError_ = true;
        return; // selection kept: the user can retry after earning more points
    }

    // SILENT refusals from the binary, in case-3 order (0x5C0C23 then 0x5C0C36):
    // morph in progress / request already in flight. No message, no trace — like
    // the binary.
    if (net::g_MorphInProgress == 1) return; // 0x5C0C23
    if (net::g_GmCmdCooldownLatch)  return; // 0x5C0C36

    // [see TODO above] 202 emission impossible (npcId missing): the call is not
    // invented. NO local effect either — the binary writes neither g_LearnedSkills
    // nor g_SkillPointPool here, it waits for the server. Selection is therefore
    // KEPT (the binary only clears it after a successful send, 0x5C0C79).
    statusText_ = "Apprentissage indisponible : requiert un PNJ formateur (non câblé).";
    statusIsError_ = true;
}

bool SkillTreeWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    Rect panel, close, grid, candPanel, prevBtn, nextBtn;
    Layout(lastScreenW_, lastScreenH_, panel, close, grid, candPanel, prevBtn, nextBtn);

    const bool inside = In(panel, x, y);
    if (inside) {
        ActivateIfHit(close, grid, candPanel, prevBtn, nextBtn, x, y);
    } else {
        armedTarget_ = Target::None;
        armedGridSlot_ = -1;
        armedCandRow_ = -1;
    }
    return inside; // only consumes if the release lands inside the window
}

bool SkillTreeWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { Close(); return true; }
    return true; // de facto modal window while open: swallows other keys
}

void SkillTreeWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Rect panel, close, grid, candPanel, prevBtn, nextBtn;
    Layout(ctx.screenW, ctx.screenH, panel, close, grid, candPanel, prevBtn, nextBtn);
    RecomputeCandidates();

    const int maxPage = candidates_.empty() ? 0
        : (static_cast<int>(candidates_.size()) - 1) / kItemsPerPage;

    char buf[160];

    if (ctx.phase == UiPhase::Panels) {
        // Panel background + frame + title bar.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);
        ctx.FillRect(panel.x, panel.y, panel.w, kTitleH, kColTitleBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kTitleH, kColBorder, 1);

        // Close button.
        const bool closeHover = In(close, cursorX, cursorY);
        ctx.FillRect(close.x, close.y, close.w, close.h, closeHover ? kColBtnHover : kColClose);
        ctx.DrawFrame(close.x, close.y, close.w, close.h, kColBorder, 1);

        // D3D9 device for icon resolution (null until device is ready -> automatic
        // dot fallback in both grids below).
        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;

        // 8x5 grid of skill bar slots (real icon if resolved, else a solid colored
        // dot — same approach as WarehouseWindow).
        for (int i = 0; i < kCols * kRows; ++i) {
            const Rect r = SlotRect(grid, i);
            const uint32_t slotSkillId = bound_ ? bar_->slots[static_cast<size_t>(i)].skillId : 0;
            const bool occupied = slotSkillId != 0;
            const bool hover = In(r, cursorX, cursorY);

            gfx::GpuTexture* icon = occupied ? GetIconTex(dev, slotSkillId) : nullptr;
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColCellLearned); // neutral background under the icon
                const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                              gfx::kSpriteWhite, /*compensatePos=*/true);
            } else {
                D3DCOLOR bg;
                if (occupied) {
                    bg = hover ? kColCellLearnedHover : kColCellLearned; // fallback dot
                } else if (hover && selectedSkillId_ != 0) {
                    bg = kColSelect; // valid target for the pending selection
                } else {
                    bg = hover ? kColCellEmptyHover : kColCellEmpty;
                }
                ctx.FillRect(r.x, r.y, r.w, r.h, bg);
            }
            const D3DCOLOR frame = (occupied && hover) ? kColSelect : kColBorder;
            ctx.DrawFrame(r.x, r.y, r.w, r.h, frame, (occupied && hover) ? 2 : 1);
        }

        // "Available" node grid (full tree, paginated): real icon if resolved, else
        // a dot colored by state (Locked/Available/Learned).
        ctx.FillRect(candPanel.x, candPanel.y, candPanel.w, kCandHeaderH, kColTitleBg);
        for (int i = 0; i < kItemsPerPage; ++i) {
            const size_t idx = static_cast<size_t>(page_) * kItemsPerPage + static_cast<size_t>(i);
            if (idx >= candidates_.size()) break;
            const int id = candidates_[idx];
            const NodeState state = NodeStateOf(id);
            const Rect r = CandidateCellRect(candPanel, i);
            const bool selected = (state == NodeState::Available &&
                                    static_cast<uint32_t>(id) == selectedSkillId_);
            const bool hover = In(r, cursorX, cursorY);

            D3DCOLOR pastille, border;
            switch (state) {
                case NodeState::Learned:   pastille = kColPastilleLearned;   border = kColNodeBorderLearned;   break;
                case NodeState::Available: pastille = kColPastilleAvailable; border = kColNodeBorderAvailable; break;
                default:                   pastille = kColPastilleLocked;    border = kColNodeBorderLocked;    break;
            }

            gfx::GpuTexture* icon = GetIconTex(dev, static_cast<uint32_t>(id));
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColRowBg); // neutral background under the icon
                const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                              gfx::kSpriteWhite, /*compensatePos=*/true);
                if (state == NodeState::Locked) // dims the icon for locked nodes
                    ctx.FillRect(r.x, r.y, r.w, r.h, kColNodeDimOverlay);
            } else {
                ctx.FillRect(r.x, r.y, r.w, r.h, hover ? kColRowHover : pastille); // fallback dot
            }
            ctx.DrawFrame(r.x, r.y, r.w, r.h, selected ? kColSelect : border, selected ? 2 : 1);
        }

        // Pagination buttons.
        const bool prevHover = In(prevBtn, cursorX, cursorY);
        ctx.FillRect(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h,
                     (page_ > 0) ? (prevHover ? kColBtnHover : kColBtn) : kColBtnOff);
        ctx.DrawFrame(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h, kColBorder, 1);
        const bool nextHover = In(nextBtn, cursorX, cursorY);
        ctx.FillRect(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h,
                     (page_ < maxPage) ? (nextHover ? kColBtnHover : kColBtn) : kColBtnOff);
        ctx.DrawFrame(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h, kColBorder, 1);
        return;
    }

    // --- Text phase ---
    ctx.Text("Compétences", panel.x + 10, panel.y + 6, kColTitle);
    ctx.Text("X", close.x + 5, close.y + 2, kColText);

    // Header line: unspent skill points + active stance.
    std::snprintf(buf, sizeof(buf), "Points de compétence disponibles : %d",
                  bound_ ? self_->skillPoints : 0);
    ctx.Text(buf, panel.x + 10, panel.y + kTitleH + 3, kColText);
    if (bound_) {
        const int stance = game::Skill_GetActiveStance(*self_, morph_, *lvlTbl_);
        if (stance > 0) std::snprintf(buf, sizeof(buf), "Posture active : #%d", stance);
        else            std::snprintf(buf, sizeof(buf), "Posture active : aucune");
        ctx.Text(buf, panel.x + 300, panel.y + kTitleH + 3, kColTextDim);
    }

    // Grid content: learned skill id + tooltip on hover.
    for (int i = 0; i < kCols * kRows; ++i) {
        const Rect r = SlotRect(grid, i);
        if (!bound_) continue;
        const auto& slot = bar_->slots[static_cast<size_t>(i)];
        if (slot.skillId != 0) {
            const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, static_cast<int>(slot.skillId));
            FormatSkillLabel(buf, sizeof(buf), rec, slot.skillId, /*maxChars=*/6); // 40px cell
            ctx.Text(buf, r.x + 3, r.y + 3, kColText);
            if (In(r, cursorX, cursorY)) {
                std::snprintf(buf, sizeof(buf), "Coût d'apprentissage : %d SP", slot.spCost);
                ctx.Text(buf, grid.x, grid.y + grid.h + 4, kColTextDim);
                const int mpCost = itemTbl_
                    ? game::Skill_CostById(static_cast<int>(slot.skillId), *self_, *itemTbl_) : 0;
                std::snprintf(buf, sizeof(buf), "Coût MP nominal (cast) : %d", mpCost);
                ctx.Text(buf, grid.x, grid.y + grid.h + 18, kColTextDim);
            }
        } else if (selectedSkillId_ != 0 && In(r, cursorX, cursorY)) {
            ctx.Text("+", r.x + r.w / 2 - 3, r.y + r.h / 2 - 6, kColSuccess);
        }
    }

    // "Available" node grid: full tree, each node shows its id + required level
    // directly on the cell; hovering shows state/cost details below the grid.
    ctx.Text("Disponibles", candPanel.x + 4, candPanel.y + 2, kColTitle);
    int hoveredNodeId = 0;
    for (int i = 0; i < kItemsPerPage; ++i) {
        const size_t idx = static_cast<size_t>(page_) * kItemsPerPage + static_cast<size_t>(i);
        if (idx >= candidates_.size()) break;
        const Rect r = CandidateCellRect(candPanel, i);
        const int id = candidates_[idx];
        const int minLvl = lvlTbl_ ? lvlTbl_->Min(id) : 0;
        const NodeState state = NodeStateOf(id);
        const D3DCOLOR idColor = (state == NodeState::Locked) ? kColTextDim : kColText;

        const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, id);
        FormatSkillLabel(buf, sizeof(buf), rec, static_cast<uint32_t>(id), /*maxChars=*/8); // 40px cell
        ctx.Text(buf, r.x + 2, r.y + 1, idColor);
        std::snprintf(buf, sizeof(buf), "Lv%d", minLvl);
        ctx.Text(buf, r.x + 2, r.y + r.h - 11, idColor);
        if (state == NodeState::Learned)
            ctx.Text("OK", r.x + r.w - 16, r.y + r.h - 11, kColSuccess);

        if (In(r, cursorX, cursorY)) hoveredNodeId = id;
    }
    if (candidates_.empty()) {
        ctx.Text("Aucune compétence connue dans la table SKILL_INFO.",
                  candPanel.x + 4, candPanel.y + kCandHeaderH + 4, kColTextDim);
    }

    // Detail for the hovered node (name/id/level/cost/prereqs/state), below the grid.
    if (hoveredNodeId != 0) {
        const NodeState state = NodeStateOf(hoveredNodeId);
        const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, hoveredNodeId);
        const int spCost = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffSpCost) : 0;
        const int minLvl = lvlTbl_ ? lvlTbl_->Min(hoveredNodeId) : 0;
        const int maxLvl = lvlTbl_ ? lvlTbl_->Max(hoveredNodeId) : 0;
        // Real name + id (SKILL_INFO data, see skillinfo::kOffName).
        FormatSkillLabel(buf, sizeof(buf), rec, static_cast<uint32_t>(hoveredNodeId), /*maxChars=*/40);
        char nameBuf[80];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s (#%d)", buf, hoveredNodeId);
        const int detailY = candPanel.y + kCandHeaderH + kCandRows * kCellPitch - kCellGap;
        ctx.Text(nameBuf, candPanel.x + 4, detailY + 4, kColTitle);
        // Required level + SP cost.
        std::snprintf(buf, sizeof(buf), "Niveau %d..%d - %d SP", minLvl, maxLvl, spCost);
        ctx.Text(buf, candPanel.x + 4, detailY + 18, kColTextDim);
        // Prerequisites (weapon/branch/section) — real SKILL_INFO fields, 0 = none.
        const int reqWeapon = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffReqWeapon) : 0;
        const int reqBranch = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffReqBranch) : 0;
        const int section    = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffSection)   : 0;
        std::snprintf(buf, sizeof(buf), "Prérequis : arme %d, branche %d, section %d",
                      reqWeapon, reqBranch, section);
        ctx.Text(buf, candPanel.x + 4, detailY + 32, kColTextDim);
        const char* stateTxt = (state == NodeState::Learned) ? "Deja apprise"
                              : (state == NodeState::Available) ? "Apprenable maintenant"
                              : "Verrouille (niveau insuffisant)";
        const D3DCOLOR stateCol = (state == NodeState::Learned) ? kColSuccess
                                 : (state == NodeState::Available) ? kColNodeBorderAvailable
                                 : kColError;
        ctx.Text(stateTxt, candPanel.x + 4, detailY + 46, stateCol);
    }

    std::snprintf(buf, sizeof(buf), "Page %d / %d", page_ + 1, maxPage + 1);
    ctx.Text(buf, candPanel.x + 4, candPanel.y + candPanel.h - kCandBtnH - 14, kColTextDim);
    ctx.Text("< Préc", prevBtn.x + 6, prevBtn.y + 2, kColText);
    ctx.Text("Suiv >", nextBtn.x + 6, nextBtn.y + 2, kColText);

    // Window footer: status of the last action, or default instruction.
    if (!statusText_.empty()) {
        ctx.Text(statusText_.c_str(), panel.x + 10, panel.y + panel.h - kFooterH + 6,
                 statusIsError_ ? kColError : kColSuccess);
    } else {
        ctx.Text("Sélectionnez une compétence disponible puis cliquez un emplacement libre.",
                 panel.x + 10, panel.y + panel.h - kFooterH + 6, kColTextDim);
    }
    if (selectedSkillId_ != 0) {
        std::snprintf(buf, sizeof(buf), "Compétence en attente d'apprentissage : #%u", selectedSkillId_);
        ctx.Text(buf, panel.x + 10, panel.y + panel.h - kFooterH + 20, kColTextDim);
    }
}

} // namespace ts2::ui

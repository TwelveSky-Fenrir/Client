// UI/ConsumableBarWindow.cpp — see UI/ConsumableBarWindow.h for the contract and,
// ABOVE ALL, the "SLOT SOURCE OF TRUTH" banner (the bar reads g_Container5,
// not the `slots` parameter).
#include "UI/ConsumableBarWindow.h"
#include "Core/Log.h"
#include "Core/Types.h"        // ts2::kRefWidth (centered/fixed branch threshold, cf. ComputeLayout)
#include "Config/GameOptions.h" // config::g_Options.MorphUiMode (g_MorphUiMode 0x84DEF4)
#include "Game/ClientRuntime.h" // game::g_Client.Var/VarGet (g_Container5, page, selected slot)
#include "Game/GameDatabase.h"  // game::GetItemInfo / game::GetSkillInfo (mITEM / mSKILL)
#include "Gfx/Renderer.h"       // ctx.renderer->Device()
#include "Gfx/SpriteBatch.h"    // ctx.sprites->DrawSprite (native-size blit, cf. Sprite2D_Draw)

#include <cstdio>

namespace ts2::ui {

namespace {

// Screen anchor — UI_GameHud_Render 0x67A3C0, quickbar block 0x684CA8-0x685177.
//   0x684C9E : cmp ds:nWidth, 400h ; 0x684CA8 : jg short loc_684CC5
//     -> if nWidth > 1024 (ts2::kRefWidth): centered branch (this[0]=nWidth/2-width/2,
//        this[1]=nHeight-height, via Sprite2D_GetWidth/Height @0x684CD1-0x684D08);
//     -> OTHERWISE (nWidth <= 1024, standard launch case "/0/0/2/1024/768", CLAUDE.md):
//        LITERAL ANCHOR this[0]=0x187=391, this[1]=0x2D8=728 @0x684CB0/0x684CBC.
// Slot i icon: (this[0] + i*0x1E + 0x18, this[1] + 7) — IDENTICAL across the 3
// content branches (skill @0x684F11-0x684F3D, tab-icon @0x685010-0x685042,
// item @0x685099-0x6850CB): pitch 0x1E = 30, FIXED offset (+24,+7).
// Stack counter (text): (this[0]+i*30+0x26, this[1]+0x15) = (+38,+21) @0x685121-0x685150.
// Page number (text)   : (this[0]+9, this[1]+0Fh) = (+9,+15) @0x684DA2-0x684DC9.
constexpr int kSlotPitch   = 30; // 0x1E
constexpr int kIconOffsetX = 24; // +0x18, verified 0x684F29/0x684FB3/0x68502E/0x6850B7
constexpr int kIconOffsetY = 7;  // +7,    verified 0x684F14/0x684F9A/0x685019/0x6850A2
constexpr int kCountOffsetX= 38; // +0x26, @0x68513F
constexpr int kCountOffsetY= 21; // +0x15, @0x68512A
constexpr int kPageOffsetX = 9;  // +9,    @0x684DB9
constexpr int kPageOffsetY = 15; // +0Fh,  @0x684DAD
// kSlotSize is NOT statically readable (the icon sprite is loaded
// dynamically) — derived from the real pitch of 30, like GameHud::InitLayout. Used
// ONLY for the hit-test/blocking rect: rendering blits at native size.
constexpr int kSlotSize = 26;
constexpr int kBarPad   = 4;

// g_Container5 addresses and cursors — cf. banner in UI/ConsumableBarWindow.h.
constexpr uint32_t kC5ItemId   = 0x16743FCu; // g_Container5_ItemId  @0x684E55/0x684ECF/0x684FE7/0x685061
constexpr uint32_t kC5Count    = 0x1674400u; // dword_1674400        @0x684EB2/0x684F70/0x6850E4/0x685103
constexpr uint32_t kC5Type     = 0x1674404u; // dword_1674404        @0x684E0B
constexpr uint32_t kVarPage    = 0x1675B1Cu; // dword_1675B1C        @0x684D85/0x684DF6
constexpr uint32_t kVarSelSlot = 0x1675B20u; // dword_1675B20        @0x684E81
constexpr int      kPageStride = 0xA8;       // 168 = 14 slots * 3 dwords * 4 bytes @0x684DFC
constexpr int      kSlotStride = 0xC;        // 3 dwords              @0x684E08

// .IMG file numbers of the quickbar block's named sprites.
// Method (identical to UI/BuffStatusPanel.h, extended to the 3 categories): the global
// Sprite2D table is built by AssetMgr_InitAllSlots 0x4DEB50 on `this` =
// 0x8E8B30; each category is a contiguous sub-array with stride 148:
//   cat.1 @0x4DEB97: `Sprite2D_BuildPath(this + 148*i + 32, 1, i, 0)`, 4500 entries
//         -> base 0x8E8B30 + 32 = 0x8E8B50 (== g_AssetMgr_UiAtlasSlots)
//   cat.2 @0x4DEBD4: `... (this + 148*j + 666032, 2, j, 0)`, 4000 entries
//         -> base 0x8E8B30 + 666032 = 0x98B4E0 (== g_AssetMgr_ItemIconSlots)
//   cat.3 @0x4DEC11: `... (this + 148*k + 1258032, 3, k, 0)`, 760 entries
//         -> base 0x8E8B30 + 1258032 = 0xA1BD60 (== unk_A1BD60, SKILL atlas)
// For an address A within a category: slot = (A - base)/148 (EXACT division) and
// file = slot + 1 (`a3 + 1` parameter of Sprite2D_BuildPath 0x4D68E0).
//   unk_8EA020: (0x8EA020 - 0x8E8B50)/148 = 5328/148 = 36  (exact) -> cat.1 file 37
//   unk_940388: (0x940388 - 0x8E8B50)/148 = 358456/148 = 2422 (exact) -> cat.1 file 2423
constexpr int kBgMorphFile  = 37;   // unk_8EA020 @0x684D5C — background if g_MorphUiMode == 1
constexpr int kBgNormalFile = 2423; // unk_940388 @0x684D7B — normal background (= size reference)

// Cooldown overlay: `Sprite2D_Draw(g_AssetMgr_UiAtlasSlots + 148*(v + 0x268), ...)`
// @0x684FB4-0x684FC9 -> slot 616 + v -> file 617 + v, v in [0,10] (11 steps).
constexpr int kCooldownFile0 = 617; // 0x268 = 616, +1 (a3+1 convention)
// dbl_7A9C48 = 0x4026000000000000 = 11.0 exactly (get_global_value) @0x684F77.
constexpr double kCooldownSteps = 11.0;

// Text color: UI_DrawNumberValue(unk_1685740, str, x, y, 1) — the last
// parameter (1) is a font/color selector of the unk_1685740 object, not
// modeled in C++ (ctx.Text only exposes a D3DCOLOR). White = neutral.
constexpr D3DCOLOR kTextWhite = 0xFFFFFFFFu;

} // namespace

// Reading a g_Container5 slot — anchor 0x684DF6-0x684E12.
ConsumableBarWindow::LiveSlot ConsumableBarWindow::ReadContainer5(int page, int i) {
    const uint32_t off = static_cast<uint32_t>(kPageStride * page + kSlotStride * i);
    LiveSlot s;
    s.refId = game::g_Client.VarGet(kC5ItemId + off);
    s.count = game::g_Client.VarGet(kC5Count  + off);
    s.type  = game::g_Client.VarGet(kC5Type   + off);
    return s;
}

// Layout — 14 slots (0x684DE9 `cmp var_438, 0Eh`), anchor cf. banner above.
ConsumableBarWindow::Layout ConsumableBarWindow::ComputeLayout(int screenW, int screenH) {
    Layout lo;
    const int totalIconsW = (kBarSlotCount - 1) * kSlotPitch + kSlotSize;

    int anchorX, anchorY;
    if (screenW > ts2::kRefWidth) {
        // Centered branch @0x684CC5-0x684D08: nWidth/2 - background width/2; nHeight -
        // background height. Background sprite width/height not statically readable ->
        // approximated by the slot extent + icon offset (same accepted limitation as
        // GameHud::InitLayout, which uses the same formula).
        anchorX = screenW / 2 - (totalIconsW + kIconOffsetX) / 2;
        anchorY = screenH - (kSlotSize + kIconOffsetY + kBarPad);
    } else {
        // Fixed branch @0x684CB0/0x684CBC — LITERAL values from the binary (standard
        // launch resolution "/0/0/2/1024/768", cf. CLAUDE.md).
        anchorX = 391;
        anchorY = 728;
    }

    const int iconX0 = anchorX + kIconOffsetX;
    const int iconY0 = anchorY + kIconOffsetY;

    lo.bar = SlotRect{ anchorX - kBarPad, anchorY - kBarPad,
                        kIconOffsetX + totalIconsW + 2 * kBarPad, kSlotSize + kIconOffsetY + 2 * kBarPad };
    for (int i = 0; i < kBarSlotCount; ++i) {
        lo.cells[static_cast<size_t>(i)] =
            SlotRect{ iconX0 + i * kSlotPitch, iconY0, kSlotSize, kSlotSize };
    }
    return lo;
}

// Textures — Sprite2D_BuildPath 0x4D68E0 (cf. declaration in the .h).
gfx::GpuTexture* ConsumableBarWindow::GetCatTex(const UiContext& ctx, int category, int fileNo) {
    if (!ctx.renderer || !ctx.renderer->Device()) return nullptr;
    if (fileNo <= 0) return nullptr;

    const char* dir = nullptr;
    switch (category) {
        case 1: dir = "001"; break; // generic UI atlas      @0x4D6945
        case 2: dir = "002"; break; // item icons            @0x4D6965
        case 3: dir = "003"; break; // skill icons           @0x4D6985
        default: return nullptr;
    }
    char path[64];
    std::snprintf(path, sizeof(path), "G03_GDATA\\D01_GIMAGE2D\\%s\\%s_%05d.IMG", dir, dir, fileNo);
    return ActiveIconCache().GetOrLoad(ctx.renderer->Device(), path);
}

gfx::GpuTexture* ConsumableBarWindow::GetIconTex(const UiContext& ctx, uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return nullptr;
    // Binary: slot = ITEM_INFO+192 - 1 (@0x68508D `mov edx,[ecx+0C0h]; sub edx,1`),
    // so file = slot + 1 = iconId.
    return GetCatTex(ctx, 2, static_cast<int>(info->iconId));
}

namespace {
// NATIVE-SIZE blit: Sprite2D_Draw 0x4D6B20 only takes (x, y) — the binary
// NEVER scales a quickbar icon.
void BlitNatural(const UiContext& ctx, gfx::GpuTexture* tex, int x, int y) {
    if (!tex || !tex->Handle() || tex->Width() == 0 || tex->Height() == 0) return;
    if (!ctx.sprites || !ctx.sprites->Ready()) return;
    ctx.sprites->DrawSprite(tex->Handle(), nullptr, x, y, gfx::kSpriteWhite);
}
} // namespace

int ConsumableBarWindow::HitTest(int mx, int my) const {
    for (int i = 0; i < kBarSlotCount; ++i) {
        if (lastLayout_.cells[static_cast<size_t>(i)].Contains(mx, my)) return i;
    }
    return -1;
}

// Decision -> message (log/caller only, NO LONGER drawn: the binary
// displays no message above the bar — cf. banner in the .h).
void ConsumableBarWindow::ApplyDecision(const game::ConsumableDecision& d, int index,
                                         const std::array<QuickSlot, kQuickSlotCount>& slots) {
    lastDecision_ = d;
    (void)slots;
    char buf[128];

    switch (d.action) {
        case game::ConsumableAction::None:
        case game::ConsumableAction::Ignored:
        case game::ConsumableAction::ArmCloseButton:
        case game::ConsumableAction::ClosePanel:
            lastMessage_.clear();
            return;

        case game::ConsumableAction::Invalid:
            std::snprintf(buf, sizeof(buf), "Case %d : objet inconnu (id %u, hors ITEM_INFO)",
                          index + 1, static_cast<unsigned>(d.refId));
            break;

        case game::ConsumableAction::Unsupported:
            std::snprintf(buf, sizeof(buf), "Case %d : competences non gerees par cette barre (TODO SkillSystem)",
                          index + 1);
            break;

        case game::ConsumableAction::ShowTooltip:
            std::snprintf(buf, sizeof(buf), "Tooltip objet %u (case %d)",
                          static_cast<unsigned>(d.refId), index + 1);
            break;

        case game::ConsumableAction::BeginItemDrag:
            // PROVEN MECHANISM (re-audit W4-F3, decompilation of UI_ConsumableBar_OnClick
            // 0x68E4B4): a click on a slot does NOT send an isolated "item use" packet
            // — it's a LOCAL drag pickup (Item_BeginDragTransaction 0x5AFDF0,
            // container type 21). The actual use happens on DROP. -> no direct
            // Net_SendOpNN to wire here; the inbound counterpart is Pkt_ItemActionDispatch (0x1a).
            std::snprintf(buf, sizeof(buf), "Prise de drag objet %u (case %d)",
                          static_cast<unsigned>(d.refId), index + 1);
            break;
    }

    lastMessage_ = buf;
    TS2_LOG("ConsumableBar : %s", lastMessage_.c_str());
}

// Mouse/keyboard events
// WIRING NOTE (hud-quickbar-buff front, W9): the binary's HUD bar CLICK path is
// cGameHud_OnMouseDown 0x62B080 — NOT reverse-engineered to date. The decisions
// below therefore still come from game::ConsumableBarLogic (port of the 28-slot
// panel 0x68E270+, a DISTINCT object — cf. banner of Game/ConsumableBarLogic.h):
// this is pre-existing code kept as-is, NOT proof for the HUD bar.
// `game::TriggerSlot` guards `index >= slots.size()` (ConsumableBarLogic.cpp:59): the
// 10..13 hit-test indices (14 real slots) return None there without overflow.
bool ConsumableBarWindow::OnMouseDown(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    (void)slots;
    // Consumes the click if it lands on the bar (blocks it from passing through to the
    // 3D scene behind the HUD); the actual action is decided on release (OnClick).
    return lastLayout_.bar.Contains(x, y);
}

bool ConsumableBarWindow::OnClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const int index = HitTest(x, y);
    if (index < 0) return lastLayout_.bar.Contains(x, y);

    const game::ConsumableDecision d = game::TriggerSlot(slots, index, /*rightClick=*/false);
    ApplyDecision(d, index, slots);
    return true;
}

bool ConsumableBarWindow::OnRightClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const int index = HitTest(x, y);
    if (index < 0) return lastLayout_.bar.Contains(x, y);

    const game::ConsumableDecision d = game::TriggerSlot(slots, index, /*rightClick=*/true);
    ApplyDecision(d, index, slots);
    return true;
}

bool ConsumableBarWindow::OnHotkey(uint8_t dikScanCode, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const game::ConsumableDecision d = game::TriggerSlotByHotkey(slots, dikScanCode);
    if (d.slotIndex < 0) return false; // scancode outside range 0x02..0x0B
    ApplyDecision(d, d.slotIndex, slots);
    return true;
}

// Render — two passes (Panels then Text), see UIManager.h.
// Port of UI_GameHud_Render 0x684D40-0x685177.
void ConsumableBarWindow::Render(const UiContext& ctx, const std::array<QuickSlot, kQuickSlotCount>& slots,
                                  int cursorX, int cursorY) {
    // `slots` (GameHud::slots_) has NO writer in src/: it is NOT the
    // source of truth (cf. banner in the .h). The bar reads g_Container5.
    (void)slots;
    // The binary draws NO hover state on the quickbar (0x684D40-0x685177:
    // no Sprite2D_HitTest) -> cursor unused.
    (void)cursorX;
    (void)cursorY;

    lastLayout_   = ComputeLayout(ctx.screenW, ctx.screenH);
    lastScreenW_  = ctx.screenW;
    lastScreenH_  = ctx.screenH;

    const int anchorX = lastLayout_.bar.x + kBarPad;
    const int anchorY = lastLayout_.bar.y + kBarPad;

    // TODO [dword_1675B1C 0x684D85 / dword_1675B20 0x684E81]: these two cursors have
    // NO writer on the C++ side (exhaustive grep: readers only) -> VarGet
    // returns 0 for both. This is NOT a degradation: these are BSS globals
    // in the binary, so they too are 0 until something writes them — the resulting
    // render is exactly the binary's in the same state (page 0 shown as "1", slot 0
    // treated as selected). Page 0 IS populated by the network
    // (Net/GameHandlers_InvCells.cpp:462-464): the bar therefore shows real slots.
    // Still to wire, outside this front: page switching and slot selection
    // (the binary's "no selection" sentinel is -1, cf.
    // Game/SkillCombat.h:237 and UI/AutoPlayWindow.cpp:224 @0x45AA6C).
    const int page = game::g_Client.VarGet(kVarPage);    // dword_1675B1C @0x684D85
    const int sel  = game::g_Client.VarGet(kVarSelSlot); // dword_1675B20 @0x684E81

    if (ctx.phase == UiPhase::Panels) {
        // --- Background: two variants depending on g_MorphUiMode (0x84DEF4) ---------------
        // `cmp ds:g_MorphUiMode, 1 / jnz short loc_684D68` @0x684D40:
        //   ==1 -> unk_8EA020 (file 37); otherwise -> unk_940388 (file 2423).
        // g_MorphUiMode is faithfully modeled by config::g_Options.MorphUiMode
        // (Config/GameOptions.h:107, offset 0x34, static_assert L.180, default 2,
        // clamped [1,2]) — xrefs_to 0x84DEF4 confirms the read @0x684D40.
        const int bgFile = (config::g_Options.MorphUiMode == 1) ? kBgMorphFile : kBgNormalFile;
        BlitNatural(ctx, GetCatTex(ctx, 1, bgFile), anchorX, anchorY);

        // --- 14 slots ----------------------------------------------------------
        for (int i = 0; i < kBarSlotCount; ++i) {
            const LiveSlot ls = ReadContainer5(page, i);
            const int x = anchorX + i * kSlotPitch + kIconOffsetX;
            const int y = anchorY + kIconOffsetY;

            if (ls.type == kSlotTypeSkill) {
                // ---- case 1: skill (loc_684E40) --------------------------
                // `SkillGrowthTbl_GetRecord(mSKILL, id)` @0x684E62; NULL -> slot
                // ignored @0x684E6D-0x684E76.
                const game::SkillInfo* rec = game::GetSkillInfo(static_cast<uint32_t>(ls.refId));
                if (!rec) continue;

                // Three states around Record[137] (= +548 = SkillInfo::field548):
                //   i != dword_1675B20                       -> +1  @0x684E89-0x684E98
                //   i == dword_1675B20 and auto-use would fire -> +2  @0x684EE5-0x684EF4
                //   i == dword_1675B20 otherwise             -> +3  @0x684EF9-0x684F08
                int tabIcon;
                if (i != sel) {
                    tabIcon = static_cast<int>(rec->field548) + 1;
                } else {
                    // TODO [AutoUse_ShouldTrigger 0x662590, called @0x684EDC with
                    // (ecx=dword_18392C0, a1=skillId, a2=count)]: predicate NOT ported.
                    // It depends on g_LearnedSkills 0x16742BC (+ levels 0x16742C0),
                    // SkillGrowthTbl_InterpStatByLevel 0x4C4EE0, Char_CalcRegen 0x4D67F0,
                    // g_EquipSnapshotScratch 0x8E719C, dword_1687378 (current HP), and
                    // dword_1673248 (equipped weapon) — none of these is modeled in
                    // C++, and porting them belongs to Game/ (outside this front).
                    // ACCEPTED DEGRADATION: we take the NEGATIVE branch (+3 = "slot
                    // selected, auto-use would not fire"). State +2 is therefore
                    // never reached — we never fabricate a false positive signal.
                    tabIcon = static_cast<int>(rec->field548) + 3;
                }
                // SKILL atlas unk_A1BD60 = base of category 3 (@0x4DEC11,
                // 0x8E8B30 + 1258032 = 0xA1BD60, exact division): slot = tabIcon,
                // so file = tabIcon + 1. Blit @0x684F31-0x684F3D.
                BlitNatural(ctx, GetCatTex(ctx, 3, tabIcon + 1), x, y);

                // ---- cooldown overlay (11 steps) --------------------------
                // Gate @0x684F42-0x684F5A: `Record[140] != Record[141]`
                // (0x230/4=140 = SkillInfo::spCost; 0x234/4=141 = SkillInfo::levelNorm).
                if (rec->spCost != rec->levelNorm) {
                    // @0x684F70-0x684F89: fild count / fmul 11.0 / fidiv levelNorm /
                    // Crt_ftol (truncation toward zero, == static_cast<int>).
                    // `fidiv` divides by an INTEGER -> explicit cast to int32 then double.
                    // levelNorm is validated [1,1000] by SkillGrowthTbl_ValidateRecord
                    // 0x4C4160: the != 0 guard is just an anti-UB safety net (the
                    // binary would raise an FP exception), never taken in practice.
                    const int32_t norm = static_cast<int32_t>(rec->levelNorm);
                    if (norm != 0) {
                        const int v = static_cast<int>(static_cast<double>(ls.count) * kCooldownSteps
                                                       / static_cast<double>(norm));
                        // @0x684FB4-0x684FC9: slot = v + 0x268 (616) of the cat.1 atlas
                        // (g_AssetMgr_UiAtlasSlots 0x8E8B50) -> file = 617 + v.
                        // Overlaid on the icon, at the SAME point (x, y). The binary does not
                        // clamp v: a v outside [0,10] would read a neighboring slot there;
                        // here the load simply fails -> nothing gets drawn.
                        BlitNatural(ctx, GetCatTex(ctx, 1, kCooldownFile0 + v), x, y);
                    }
                }
            } else if (ls.type == kSlotTypeTabIcon) {
                // ---- case 2: tab icon (loc_684FD3) ----------------------
                // TODO [cQuickSlotWin_GetTabIcon 0x662750, called @0x684FF4 with
                // (ecx=dword_18392C0, a1=itemId)]: "map tab index 1-9 to icon sprite
                // base (600-640)" — table NOT ported. The binary: `if (v == -1)
                // continue;` @0x684FFC-0x685002, then `v += 2` @0x685007-0x68500D and
                // `Sprite2D_Draw(unk_A1BD60 + 148*v, x, y)` @0x685036-0x685042 (cat.3
                // atlas, so file = v + 1). Without the table, the index is unknown ->
                // no icon drawn (silent fallback, never a fake icon).
                continue;
            } else if (ls.type == kSlotTypeItem) {
                // ---- case 3: item (loc_68504C) -------------------------------
                // `MobDb_GetEntry(mITEM, id)` @0x68506E; NULL -> slot ignored
                // @0x685079-0x685082.
                const game::ItemInfo* info = game::GetItemInfo(static_cast<uint32_t>(ls.refId));
                if (!info) continue;
                // ITEM atlas g_AssetMgr_ItemIconSlots 0x98B4E0 (category 2), base
                // DISTINCT from the skills' one. Blit @0x6850BC-0x6850CB.
                BlitNatural(ctx, GetIconTex(ctx, static_cast<uint32_t>(ls.refId)), x, y);
            }
            // type == 0 (or any other value): `jmp loc_685155` @0x684E3B -> empty
            // slot, NOTHING is drawn (not even a frame: the binary draws none
            // on this bar).
        }
    }

    if (ctx.phase == UiPhase::Text) {
        // --- Page number ----------------------------------------------------
        // @0x684D85-0x684DC9: `Crt_Vsnprintf(&String, "%d", dword_1675B1C + 1)` then
        // `UI_DrawNumberValue(unk_1685740, &String, this[0]+9, this[1]+0Fh, 1)`.
        // 1-based display.
        {
            char pageBuf[16];
            std::snprintf(pageBuf, sizeof(pageBuf), "%d", page + 1);
            ctx.Text(pageBuf, anchorX + kPageOffsetX, anchorY + kPageOffsetY, kTextWhite);
        }

        // --- Per-slot stack counter -----------------------------------------
        // @0x6850D0-0x685150, ONLY in the item branch (case 3) and only
        // if `dword_1674400[...] > 0` (`cmp ..., 0 / jle loc_685155` @0x6850E4).
        // This is the SLOT COUNTER, not game::InventoryCount(refId) (the binary
        // never queries owned inventory here).
        for (int i = 0; i < kBarSlotCount; ++i) {
            const LiveSlot ls = ReadContainer5(page, i);
            if (ls.type != kSlotTypeItem) continue;
            if (!game::GetItemInfo(static_cast<uint32_t>(ls.refId))) continue; // @0x685079
            if (ls.count <= 0) continue;                                       // @0x6850EC

            char qtyBuf[16];
            std::snprintf(qtyBuf, sizeof(qtyBuf), "%d", ls.count);
            ctx.Text(qtyBuf, anchorX + i * kSlotPitch + kCountOffsetX,
                     anchorY + kCountOffsetY, kTextWhite);
        }
    }
}

} // namespace ts2::ui

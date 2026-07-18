// UI/CharacterStatsWindow.h — "Character Sheet" window (computed stats).
//
// Displays the ALREADY COMPUTED state of game::g_World.self (StatEngine::Recompute,
// which itself invokes the 15 Char_Calc* functions rewritten in
// Game/StatFormulas.h/.cpp — see that header for the original address table). This
// window recomputes NOTHING: it reads SelfState as-is (level, 4 primary attributes,
// unspent points, derived stats maxHp/maxMp/extAtk/intAtk/extDef/intDef/
// accuracy/evasion/critRate/atkRatingMin/atkRatingMax/attackSpeed).
//
// GEOMETRY — CONFIRMED_FAITHFUL (2026-07-14, fresh idaTs2 decompilation, this
// audit pass): the original window IS identified — cDrawWin_Draw 0x629960
// (render) + cDrawWin_OnMouseDown 0x628EA0 (hit-test), both read/write
// dword_16731B8..D0 (attrDefensive/attrExtForce/attrOffensive/attrIntForce/
// g_SelfUnspentAttrPoints), i.e. EXACTLY the PrimaryAttr/unspentAttr fields
// below — this wasn't visible in the previous pass (no UI_CharSheet_* name
// in the IDB, hence the old "not found" conclusion). Verified points that
// CONTRADICT the previous implementation (centered window, close button
// top-right, 4 "+" buttons stacked in a column):
//   1) ANCHOR: NO screen centering. cDrawWin_Draw computes its origin via
//      UI_ProjectSpriteToScreen(&g_PlayerCmdController, /*spriteSlot*/297,
//      /*designX*/115, /*designY*/105, &outX, &outY) (0x629 9AA), which applies
//      exactly (disassembly 0x50F5D0):
//        outX = round((115 + w/2) * nWidth  / 1024.0f) - w/2
//        outY = round((105 + h/2) * nHeight / 768.0f)  - h/2
//      where nWidth/nHeight = 0x1669184/0x1669188 (REAL resolution, cf.
//      Core/Types.h) and 1024.0f/768.0f are the constants flt_1669178/
//      flt_166917C initialized from flt_7A68C8/flt_7A68C4 in WinMain
//      (0x4609D3..0x4609E5) — byte-exact CONFIRMED (bytes 00 00 80 44 / 00 00 40
//      44 = IEEE754 1024.0f / 768.0f). This is EXACTLY kRefWidth/kRefHeight
//      (Core/Types.h): at the 1024x768 reference resolution, the formula
//      reduces to outX=115, outY=105 (the panel is anchored near the TOP-LEFT
//      corner of the screen, NOT centered); at any other resolution, the
//      sprite's CENTER (whose pixel size is fixed, not scaled) is repositioned
//      to the same screen fraction as its design position — a "proportional
//      anchor" mechanism, different from the pure centering used by
//      SkillTreeWindow/UI_SkillLearn_Draw. The real `w`/`h` (background sprite
//      dimensions, 4 variants by tier: unk_8F3704/94B470/90E774/985AB0) remain
//      NOT statically confirmed (loaded from an .IMG at runtime); kBoxW/
//      kBoxH below therefore remain an approximation (as before), but the
//      ANCHOR FORMULA itself is now faithful.
//   2) CLOSE BUTTON: cDrawWin_OnMouseDown tests Sprite2D_HitTest(unk_8F3798,
//      *this+8, *(this+1)+6, ...) (0x629188) -> the real close button is
//      TOP-LEFT (offset (8,6) from the panel origin), NOT top-right.
//   3) ATTRIBUTE "+1" BUTTONS: real 2x2 grid (NOT a column of 4), read in
//      cDrawWin_OnMouseDown (0x628F02..0x629007) and redrawn in cDrawWin_Draw
//      (0x62A26C..0x62A3C7): FIXED offsets from the panel origin (Sprite2D
//      *this+dx, *(this+1)+dy): ExtForce (52,109), IntForce (148,109),
//      Defensive (52,131), Offensive (148,131) — left/right column = x=52/148,
//      top/bottom row = y=109/131. Matches exactly the PrimaryAttr order
//      (ExtForce=0,IntForce=1,Defensive=2,Offensive=3) mapped to a grid
//      (col=i%2, row=i/2).
//   4) ATTRIBUTE "+5" BUTTONS (wired Pass 4 / W6, formerly "out of scope"): second
//      set of 4 buttons, sprite unk_940260, offsets (67,109)/(163,109)/(67,131)/
//      (163,131) — same grid, column x=67/163. Armed only if
//      g_SelfUnspentAttrPoints >= 5 (cDrawWin_OnMouseDown 0x629027, cDrawWin_Draw
//      0x62A3D3); emit args 5/6/7/8 and decrement by 5
//      (cDrawWin_OnCommit 0x629554/0x629602/0x6296B0/0x629761).
// Remaining unconfirmed (unchanged): exact background sprite dimensions
// (kBoxW/kBoxH), exact "+"/close button size (kPlusSize/kCloseSize -
// depend on sprites unk_8F416C/940260/8F3798, loaded from an .IMG at runtime,
// not dumped). Displayed VALUES remain byte-exact (StatFormulas.h), only the
// panel's geometry changed.
// TODO [anchor 0x8F416C / 0x940260] kPlusSize=18 is an approximation: the real
// gap between the "+1" column (x=52/148) and the "+5" column (x=67/163) is only
// 15 px, so at 18 px the two rects OVERLAP by 3 px. No functional consequence
// here: the test order reproduces the binary's (the 4 "+1" BEFORE the 4 "+5",
// 0x628EDC then 0x629027), so the shared zone is attributed to "+1"
// exactly as in the original game. To fix once the sprites' real dims are dumped.
//
// Attribute point spend ("+") — PROVEN (Pass 4 / W6, fresh decompilation of
// cDrawWin_OnCommit 0x6291F0): this is NOT dispatcher 0x58 (the previous pass's
// hypothesis, now REFUTED — Net_OnCultivationDispatch 0x493180 is the INBOUND
// dispatcher that applies the result). The real emission is:
//   Net_SendVaultReq_206(arg)  // 0x590430
//     -> Net_SendPacket_Op19(&g_AutoPlayMgr, 206, &arg)  // 0x4B4E70
//     -> outbound opcode 0x13 @+8, sub-code 206 u32 LE @+9, payload 100 bytes @+13
//        of which payload[0..3] = arg int32 LE (Crt_Memcpy(v2, &a1, 4) 0x590454 — the
//        "outgoing char emitted as 4 bytes LE" gotcha), payload[4..99] = UNINITIALIZED
//        stack (_BYTE v2[108] with no memset). Total size fixed at 113 (0x71).
//   arg = 1/2/3/4 -> +1 on ExtForce/IntForce/Defensive/Offensive;
//   arg = 5/6/7/8 -> +5 on the same, in the same order.
#pragma once
#include "UI/UIManager.h"
#include "Game/StatFormulas.h"
#include "Game/GameState.h"

namespace ts2::ui {

// The 4 spendable primary attributes (window display order).
enum class PrimaryAttr : int {
    ExtForce  = 0, // attrExtForce  (ITEM_INFO offset 292)
    IntForce  = 1, // attrIntForce  (ITEM_INFO offset 296)
    Defensive = 2, // attrDefensive (ITEM_INFO offset 300)
    Offensive = 3, // attrOffensive (ITEM_INFO offset 304)
};
inline constexpr int kPrimaryAttrCount = 4;

// -----------------------------------------------------------------------------
// CharacterStatsWindow — lightweight modal character sheet (closable), not draggable.
// Reads game::g_World.self on every Render (no state duplicated on the window side).
class CharacterStatsWindow : public Dialog {
public:
    void Open() override;                       // centers + rearms the latches
    // Close() inherited as-is (bOpen_=false).

    bool OnMouseDown(int x, int y) override;     // arms close/+ if hovered
    bool OnClick(int x, int y) override;         // commits close/+ if released on top
    bool OnKey(int vk) override;                 // Escape -> closes

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };

    // Geometry recomputed every frame (centered on the current screen).
    struct Layout {
        Rect box;                                 // full panel
        Rect titleBar;                             // title bar
        Rect closeBtn;                             // close button (top-right corner)
        Rect plusBtn[kPrimaryAttrCount];            // "+1" button per primary attribute
        Rect plus5Btn[kPrimaryAttrCount];           // "+5" button per primary attribute (0x62904D..)
    };
    void ComputeLayout(int screenW, int screenH, Layout& L) const;

    // Emits the attribute point spend and applies the binary's local effects.
    // `arg` = value sent to the server (1..4 for "+1", 5..8 for "+5"); `cost` =
    // decrement applied to unspentAttr (1 or 5). Reproduces the body of each
    // branch of cDrawWin_OnCommit 0x6291F0 — see .cpp for the anchor details.
    void CommitAttrSpend(int arg, int cost);

    static const char* AttrLabel(PrimaryAttr a);
    static int          AttrValue(const game::SelfState& s, PrimaryAttr a);

    // Real proportional anchor (cf. CONFIRMED_FAITHFUL banner above): design
    // position (1024x768) of the window's corner, projected to the real
    // resolution via UI_ProjectSpriteToScreen 0x50F5D0.
    static constexpr int kDesignAnchorX = 115; // a3 of the real call (0x629 9AA)
    static constexpr int kDesignAnchorY = 105; // a4 of the real call
    // Real offsets of the 4 "+1" buttons (2x2 grid), from the panel origin
    // (cDrawWin_OnMouseDown 0x628F02.., cDrawWin_Draw 0x62A26C..).
    static constexpr int kPlusOffX[2] = { 52, 148 }; // left / right column
    static constexpr int kPlusOffY[2] = { 109, 131 }; // top / bottom row
    // Real offsets of the 4 "+5" buttons — sprite unk_940260, same rows as "+1"
    // (kPlusOffY), shifted columns (cDrawWin_OnMouseDown 0x62904D/0x62909D/
    // 0x6290EC/0x62913E, cDrawWin_OnCommit 0x629514/0x6295C2/0x629670/0x629721).
    static constexpr int kPlus5OffX[2] = { 67, 163 }; // left / right column
    // Real close button: offset (8,6) from the panel origin (TOP-LEFT,
    // NOT top-right) — cDrawWin_OnMouseDown 0x629188.
    static constexpr int kCloseOffX = 8;
    static constexpr int kCloseOffY = 6;

    // "Mouse-down -> released on top" latches (original MsgBoxDialog/UI pattern).
    // Mirrors of *(this+3..this+11) from cDrawWin_OnMouseDown/OnCommit 0x628EA0/0x6291F0.
    bool closeArmed_ = false;
    bool plusArmed_[kPrimaryAttrCount] = {false, false, false, false};   // *(this+3..+6)
    bool plus5Armed_[kPrimaryAttrCount] = {false, false, false, false};  // *(this+7..+10)

    // Screen dims cached from the last Render, so the hit-test (routed
    // across two frames) aligns with the actually drawn geometry.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

} // namespace ts2::ui

// UI/PanelSkin.h — textured panel background for UI windows (ts2::ui), with automatic
// fallback to UiContext::FillRect if the texture is unavailable. Modifies NEITHER
// Dialog NOR UiContext (UI/UIManager.h): additive utility shared by the 11 windows
// that have a panel background, to avoid duplicating the same load-once + fallback
// logic 11 times.
//
// -----------------------------------------------------------------------------
// TEXTURE PROVENANCE — METHOD PROVEN BY DISASSEMBLY (revision W9).
// -----------------------------------------------------------------------------
// A previous version of this banner claimed "no idaTs2 MCP tool was available" and
// that the file -> window mapping came from a "STATISTICAL analysis" of .IMG sizes.
// That justification is OBSOLETE: the slot -> file chain is now established
// instruction by instruction, and the literal indices it used to excuse are WRONG.
// Ground truth:
//
//   1. AssetMgr_UpdateUnloadExpired 0x4E2050: `imul ecx, 94h` (=148, a Sprite2D's
//      stride) then `lea ecx, [edx+ecx+20h]` with edx = g_ModelMotionArray
//      0x8E8B30  =>  UI atlas pool base = 0x8E8B30 + 0x20 = 0x8E8B50
//      (IDB symbol `g_AssetMgr_UiAtlasSlots`), 0x1194 = 4500 slots, category 1.
//   2. Sprite2D_BuildPath 0x4D6900 case 1 (@0x4d6913):
//        g_UseTRVariant (0x1669190) == 1 -> "G03_GDATA\D01_GIMAGE2D\001\TR\001_%05d.IMG" (@0x4d6928)
//        otherwise                       -> "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"    (@0x4d6945)
//      with %05d = (slot index) + 1.
//
//   => slot = (unk_address - 0x8E8B50) / 148, the division MUST come out even
//      (remainder 0); the file number equals slot + 1.
//
// Slots ACTUALLY proven by xref (to substitute for guesses; the wiring lives in the
// windows' .cpp files, cf. the wiring TODO further below):
//
//   unk_8F7608 -> (0x8F7608-0x8E8B50)/148 = 406,  remainder 0 -> 001_00407.IMG
//       GENERIC TEMPLATE, NOT "the guild background": xrefs_to 0x8F7608 shows it is
//       SHARED by UI_ClanWin_Draw 0x5DA210 (@0x5da28c) AND UI_NpcMenu_Draw
//       0x5DFC30 (@0x5dfdb1) (+ their OnLDown/OnLUp). Labeling it per-window would be
//       wrong: several dialogs reuse the same template.
//   unk_94B87C -> (0x94B87C-0x8E8B50)/148 = 2735, remainder 0 -> 001_02736.IMG
//       Shop/gems panel: UI_Shop_Render 0x5C7E44 (@0x5c7ed6) and
//       UI_Shop_ShowItemTooltip 0x5C9360 (@0x5c9386).
//   unk_9404B0 -> (0x9404B0-0x8E8B50)/148 = 2424, remainder 0 -> 001_02425.IMG
//       13x15 tooltip background tile (Item_DrawTooltip 0x652AD0 @0x65e305),
//       consumed by UI/ItemTooltip.cpp via the `Cat1Slot` constructor below.
//
// TODO [anchors 0x5DA210 / 0x5C7E44 / … ] — WIRING OUTSIDE THIS FILE: the 11
// `PanelSkin kPanelBg("…001_XXXXX.IMG")` literals live in window .cpp files
// (GuildWindow.cpp:23, VendorShopWindow.cpp:62, CharacterStatsWindow.cpp:28,
// SocialWindow.cpp:17, SkillTreeWindow.cpp:63, PlayerTradeWindow.cpp:27,
// QuestTrackerWindow.cpp:29, PartyWindow.cpp:25, NpcDialogWindow.cpp:28,
// OptionsWindow.cpp:18, AutoPlayWindow.cpp:29) and remain GUESSES until they are
// replaced with `PanelSkin::Cat1Slot{<proven slot>}`. Method to apply for each:
// decompile its real UI_*_Draw, spot the BACKGROUND call
// `Sprite2D_Draw(&unk_X, *this, *(this+1))` preceded by the centering
// `Sprite2D_GetWidth/Height(&unk_X)`, compute (unk_X - 0x8E8B50)/148 and verify the
// remainder is 0. Only 406 / 2735 / 2424 are proven so far.
#pragma once
#include "UI/UIManager.h"
#include "Gfx/GpuTexture.h"
#include <string>

namespace ts2::ui {

// ---------------------------------------------------------------------------
// Mirror of g_UseTRVariant (dword_1669190), initialized to 0 by WinMain @0x4609FB
// and set to 1 when field 1 of the command line equals 1 (cf. App/GameConfig.h
// `useTRVariant`, App.cpp:83). Consulted by Sprite2D_BuildPath 0x4D6900 for
// categories 1 (@0x4d6913) and 4 (@0x4d6999) ONLY — categories 2/3/5/6/7 have no
// TR branch (gap TEX-2).
//
// TODO [anchor 0x4609FB] — WIRING OUTSIDE THIS FILE: App::Init must call
// `ts2::ui::SetUseTRVariant(cfg_.useTRVariant != 0);` right after parsing the
// command line (App/App.cpp, next to line 363
// `gfx::Font::AddTtfResource(cfg_.useTRVariant != 0)`, which already applies the same
// field to fonts). Until this hook is added, the value stays `false` = non-TR branch =
// EXACT default EU build behavior (`/0/0/2/…` gives field[1]=0), so no regression;
// only a TR launch would remain inconsistent.
void SetUseTRVariant(bool on);

// Builds the path of a UI atlas category-1 file from its 0-based SLOT index — exact
// replica of the two branches of Sprite2D_BuildPath 0x4D6900 case 1 (@0x4d6928 /
// @0x4d6945), file number = slot + 1. Returns an empty string if `slot` is negative.
std::string Cat1SlotPath(int slot);

// ---------------------------------------------------------------------------
// Lazy textured panel background (1 load attempt, memorized). Two ways to build it:
//   static const PanelSkin s_bg(PanelSkin::Cat1Slot{2424});   // PREFERRED (proven)
//   static const PanelSkin s_bg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01165.IMG");
// The `Cat1Slot` form is the only faithful one: it goes through Cat1SlotPath() and
// therefore honors the TR variant, exactly like the binary.
class PanelSkin {
public:
    // 0-based slot index in the UI atlas category-1 pool (base 0x8E8B50,
    // 4500 slots) — cf. the header banner for the derivation.
    struct Cat1Slot { int slot; };

    explicit PanelSkin(const char* imgRelPath) : path_(imgRelPath ? imgRelPath : "") {}
    explicit PanelSkin(Cat1Slot s) : slot_(s.slot) {}

    // ---- NATURAL-SIZE blit (faithful) -----------------------------------
    // Sprite2D_Draw 0x4D6B20 calls UI_DrawSprite(this+104, x, y, 0,0,0,0,0)
    // (@0x4D6B72): NO scale factor at all. This is the primitive used by every panel
    // background and by the tooltip tile. Draws the texture at its real size at (x,y)
    // and returns true; returns false WITHOUT DRAWING ANYTHING if the texture is
    // unavailable (the caller decides its own fallback). Panels phase only.
    bool Draw(const UiContext& ctx, int x, int y) const;

    // Natural texture size (0 if unavailable). Lets windows DERIVE their geometry from
    // the sprite instead of making it up, as in UI_Shop_ShowItemTooltip 0x5C9360:
    //     *this     = nWidth/2  - (u16)Sprite2D_GetWidth(&unk_94B87C)/2   @0x5c939d
    //     *(this+1) = nHeight/2 - (u16)Sprite2D_GetHeight(&unk_94B87C)/2  @0x5c93c2
    // Loads the texture on first call (like Draw).
    uint32_t TexW(const UiContext& ctx) const;
    uint32_t TexH(const UiContext& ctx) const;

    // ---- STRETCHED blit (NOT faithful — kept for the 11 existing callers) --
    // TODO [anchor 0x4D6B20] — PROVEN DIVERGENCE (gap TT-06), fix OUTSIDE THIS FILE.
    // This overload stretches the texture to a (w,h) rect that each window makes up
    // (kPanelW/kPanelH constants in its .h) via SpriteBatch::DrawSpriteScaled. The
    // binary does the OPPOSITE: the window size IS the sprite's size (cf.
    // 0x5c939d/0x5c93c2 above), and the blit has no scale (0x4D6B72). A scaling
    // primitive does exist (Sprite2D_DrawScaled 0x4D6BF0) but it is NOT the one used
    // for panels. It is KEPT AS-IS because the 11 windows calling it anchor their
    // widgets on kPanelW/kPanelH: removing it (or making it blit at natural size)
    // would misalign all their content with no way for this front to fix it.
    // Migration to be done window by window, each in its own .cpp: replace
    // `Draw(ctx, x, y, w, h, col)` with `Draw(ctx, x, y)`, derive the origin via
    // TexW/TexH ((ctx.screenW - TexW)/2, (ctx.screenH - TexH)/2, integer division)
    // and remove kPanelW/kPanelH.
    bool Draw(const UiContext& ctx, int x, int y, int w, int h, D3DCOLOR fallbackColor) const;

private:
    void EnsureLoaded(const UiContext& ctx) const;

    mutable std::string      path_;          // resolved path (or supplied as-is)
    int                      slot_ = -1;     // >= 0: resolved lazily via Cat1SlotPath
    mutable gfx::GpuTexture  tex_;
    mutable bool             tried_ = false; // load attempted (success or failure) once
};

} // namespace ts2::ui

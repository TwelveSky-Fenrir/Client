// UI/GameHud_Vitals.cpp — GameHud vitals-frame sub-passes (split out of GameHud.cpp
// for size; same class, see GameHud.cpp for the file-family banner and EA history).
// Holds DrawVitalsFrame (frame + portrait + the 4 vitals bars), DrawTalismanBadge
// (§4 talisman badge) and DrawQuickSlotFrames (quickbar fallback rendering).
#include "UI/GameHud.h"
#include "UI/GameHud_Internal.h" // shared constants/helpers (kHpBg/kHpFill/kMpBg/kMpFill, kMasteryMax, kVitalsBarSteps, kBody*, RdBodyI32, ComputeExpProgress)
#include "Game/GameState.h"      // game::g_World (self.*, players[0] = faithful bar source, cf. wave W9 §2)
#include "Game/ConsumableBarLogic.h" // game::InitConsumableBar (G01 wiring, cf. GameHud.cpp::Init())
#include "Game/GameDatabase.h"   // game::GetLevelInfo/LevelInfo (§1 EXP bar, mission W4-F2)
#include "Game/ActionStateMachine.h" // game::CharActionState (CastSlot0-2/Channel -> §16 cast indicator, mission 2026-07-14)
#include "Game/ClientRuntime.h"  // game::g_Client.msg (MessageLog -> ChatWindow, mission 2026-07-14)
#include "Game/StringTables.h"   // game::g_Strings.zoneNames (§17 quest marker callout, mission 2026-07-14)
#include "Net/NetClient.h"       // net::g_GmAuthLevel (§17 GM debug overlay, mission 2026-07-14)
#include "Core/Log.h"
#include "Asset/ImgFile.h"    // .IMG loading (zlib wrapper + DXT FourCC)
#include "UI/BuffStatusPanel.h" // §9 buff grid + §16 bottom-right panel (mission 2026-07-14)
#include "UI/ConsumableBarWindow.h" // §14 real quickbar (mission 2026-07-14, see GameHud.cpp banner)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring> // std::memcpy (WrBodyI32 below; RdBodyI32 lives in GameHud_Internal.h)
#include <string>

namespace ts2::ui {

namespace {
// --- Vitals frame palette (ARGB) --------------------------------------------
constexpr D3DCOLOR kFrameBg     = 0xC0101018u; // frame background (translucent)
constexpr D3DCOLOR kFrameBorder = 0xFF3A3A48u; // frame outline
constexpr D3DCOLOR kPortraitBg  = 0xFF181820u; // portrait mini-frame background
constexpr D3DCOLOR kSlotBg      = 0xB0000000u; // quickslot background
constexpr D3DCOLOR kSlotBorder  = 0xFF404050u; // quickslot outline
constexpr D3DCOLOR kSlotFilled  = 0xFF2E7D46u; // "slot assigned" marker (icon placeholder)

// --- EXP + elemental mastery bars (§1, UI_GameHud_Render 0x67A59E-0x67A782) --
// Free-form style (ARGB not proven — no color table modeled client-side).
constexpr D3DCOLOR kExpBg       = 0xFF201028u; // EXP bar background (dark purple)
constexpr D3DCOLOR kExpFill     = 0xFF9A46E0u; // EXP fill (purple)
constexpr D3DCOLOR kMasteryBg   = 0xFF08201Cu; // Mastery bar background (dark green)
constexpr D3DCOLOR kMasteryFill = 0xFF2CC888u; // Mastery fill (green)

// --- Vitals bars atlas geometry (HUD-02, wave W9) ----------------------------
// Same pattern for all 4 bars in UI_GameHud_Render 0x67A3C0:
//   if (cur > 0) {                                   // gate `cmp <cur>,0 / jle`
//     v = Crt_ftol(cur * 41.0 / max);                // fmul dbl_7A9860 (=41.0) / fidiv
//     frame = v + BASE;                              // add eax, <BASE>
//     if (frame > BASE+41) frame = BASE+41;          // clamp
//     Sprite2D_Draw(&g_AssetMgr_UiAtlasSlots + 148*frame, 57, Y);
//   }
// dbl_7A9860 read back byte-for-byte = 41.0; the binary clamp is ALWAYS BASE+41
// (136/178/220/3584 for 95/137/179/3543) -> expressed here as base+kVitalsBarSteps
// (GameHud_Internal.h), not duplicated.
constexpr int kHpBarBase        = 95;   // add eax, 5Fh   @0x67A462 ; clamp 88h=136 @0x67A471
constexpr int kMpBarBase        = 137;  // add eax, 89h   @0x67A515 ; clamp 0B2h=178 @0x67A526
constexpr int kExpBarBase       = 179;  // add eax, 0B3h  @0x67A65A ; clamp 0DCh=220 @0x67A66B
constexpr int kMasteryBarBase   = 3543; // add eax, 0DD7h @0x67A707 ; clamp 0E00h=3584 @0x67A718
constexpr int kVitalsBarX       = 57;   // push 39h — constant across the 4 bars
constexpr int kHpBarY           = 8;    // push 8   @0x67A478
constexpr int kMpBarY           = 22;   // push 16h @0x67A52D
constexpr int kExpBarY          = 36;   // push 24h @0x67A672
constexpr int kMasteryBarY      = 50;   // push 32h @0x67A71F

// --- §4 talisman badge (HUD-09) — colored-rect fallback ----------------------
// FIXED anchor (190,75): `push 4Bh`(75) / `push 0BEh`(190) @0x67A790-0x67A792
// (dword_16758A4 > 0 branch, sprite unk_981A84) and the same literals in the
// talisman branch (sprite unk_9819F0). Standalone Sprite2D objects -> colored
// fallback, same policy as GameHud_Render.cpp's §15 menu buttons.
constexpr int      kTalismanBadgeX = 190; // push 0BEh @0x67A792
constexpr int      kTalismanBadgeY = 75;  // push 4Bh  @0x67A790
constexpr int      kTalismanBadgeSz = 20; // size unreadable (native Sprite2D) — fallback
constexpr D3DCOLOR kTalismanActive  = 0xFFD8A030u; // dword_16758A4 > 0 (unk_981A84)
constexpr D3DCOLOR kTalismanSlotted = 0xFF3A8A46u; // valid talisman id range (unk_9819F0)

// Writes a signed int32 into an entity's raw body (PlayerEntity::body, entity+0x18).
// Companion of RdBodyI32 (GameHud_Internal.h); used only here (the HP-clamp side
// effect below), so kept local rather than promoted to the shared header.
void WrBodyI32(std::array<uint8_t, 600>& body, size_t off, int32_t v) {
    std::memcpy(body.data() + off, &v, sizeof(v));
}
} // namespace

// =============================================================================
// Sub-passes
// =============================================================================
void GameHud::DrawVitalsFrame() {
    // Frame: real .IMG sprite if loaded (full-frame blit at native size, faithful
    // to Sprite2D_Draw(&unk_8EC114, 0, 0) which blits at its native size — cf.
    // GameHud.cpp file header banner). Falls back to the old flat rect + outline
    // otherwise: a load failure never breaks the previously working behavior.
    if (vitalsFrameTex_.Valid()) {
        sprite_.DrawSprite(vitalsFrameTex_.Handle(), nullptr, layout_.frame.x, layout_.frame.y);
    } else {
        DrawFilledRect(layout_.frame, kFrameBg);
        DrawBorder(layout_.frame, 1, kFrameBorder);
    }

    // Portrait mini-frame (placeholder: flat background + outline; the character
    // icon will come from the .IMG skin once wired).
    DrawFilledRect(layout_.portrait, kPortraitBg);
    DrawBorder(layout_.portrait, 1, kFrameBorder);

    // --- HP/MP bars (HUD-02 + HUD-VIT-01 + HUD-VIT-02, wave W9) ---------------
    // FAITHFUL SOURCE = the player-array fields (index 0 = self), NOT
    // game::g_World.self (a parallel store never resynced by the Net layer — cf.
    // GameHud.cpp banner §2):
    //   HP: cur body+292 (dword_1687370) / max body+288 (dword_168736C) @0x67A442-0x67A457
    //   MP: cur body+300 (dword_1687378) / max body+296 (dword_1687374) @0x67A4F5-0x67A50A
    // The RAW BODY is read, not the plain players[0].hp/.mp fields: the latter are
    // only a MIRROR of body+292/+300 set by Net/CharStatDeltaDispatch.cpp::ReflectBars,
    // whereas body+292/+300 literally ARE dword_1687370/dword_1687378, the globals the
    // binary reads. Reading the source rather than the mirror avoids any drift if a
    // path writes the body without re-mirroring it (and the MAX fields have no plain
    // mirror at all anyway). g_World.Self() guarantees index 0 exists (emplaces if the
    // array is empty).
    game::PlayerEntity& p0 = game::g_World.Self();
    const int hpCur = RdBodyI32(p0.body, kBodyHpCur);
    const int hpMax = RdBodyI32(p0.body, kBodyHpMax);
    const int mpCur = RdBodyI32(p0.body, kBodyMpCur);
    const int mpMax = RdBodyI32(p0.body, kBodyMpMax);

    // Gate `cmp dword_1687370,0 / jle loc_67A490` @0x67A442: the bar is only drawn
    // if current HP > 0. Atlas first, quantized fallback if the .IMG is missing.
    if (hpCur > 0) {
        if (!DrawAtlasBar(kHpBarBase, hpCur, hpMax, kVitalsBarX, kHpBarY))
            DrawBarFillQuantized(layout_.hpBar, hpCur, hpMax, kVitalsBarSteps, kHpBg, kHpFill);
    }

    // HUD-VIT-06(a) — SOURCE-REWRITING clamp @0x67A490-0x67A499:
    //   `cmp dword_1687370,0 / jge loc_67A4A3 / mov dword_1687370,0`
    // A PERSISTENT side effect of the render path, placed AFTER the bar and BEFORE
    // the "%d/%d" text (@0x67A4A3-0x67A4B0): the text therefore shows "0/max", never
    // a negative value. Both representations are written (body+292 = the binary's
    // global, and its plain mirror p0.hp) to keep them in sync — ReflectBars keeps
    // them equal elsewhere.
    if (hpCur < 0) {
        WrBodyI32(p0.body, kBodyHpCur, 0);
        p0.hp = 0;
    }

    // Gate `cmp dword_1687378,0 / jle loc_67A545` @0x67A4F5.
    if (mpCur > 0) {
        if (!DrawAtlasBar(kMpBarBase, mpCur, mpMax, kVitalsBarX, kMpBarY))
            DrawBarFillQuantized(layout_.mpBar, mpCur, mpMax, kVitalsBarSteps, kMpBg, kMpFill);
    }

    // §1 EXP bar + elemental mastery bar (mission W4-F2, UI_GameHud_Render
    // 0x67A59E-0x67A782). LOCAL rects derived from layout_.frame (fallback rects
    // only — the normal path goes through the atlas, absolute-anchored at x=57
    // like the binary): same x=57 / width 150 / 14px pitch as hpBar (y=8) / mpBar
    // (y=22), faithful to the fill anchors (57,36) @0x67A672 and (57,50) @0x67A71F.
    const game::SelfState& self = game::g_World.self; // level/levelBonus: outside the bars, cf. §2
    const HudRect& fr = layout_.frame;
    const HudRect expBar{ fr.x + kVitalsBarX, fr.y + kExpBarY, 150, 12 };
    const HudRect masteryBar{ fr.x + kVitalsBarX, fr.y + kMasteryBarY, 150, 12 };

    int expProgress = 0, expSpan = 0;
    ComputeExpProgress(self.level, self.levelBonus, expProgress, expSpan);
    // Faithful gate `cmp var_84C,0 / jle` @0x67A641: drawn only if progress > 0.
    // NB: `&& expSpan > 0` is an ADDITION vs. the binary (C++-side div/0 guard —
    // the binary divides by var_850 without testing it, cf. fidiv @0x67A64F). Since
    // GetRebirthExpSpan was wired, span is non-zero on both branches (rebirth and
    // normal): this guard no longer alters rendering. Kept as a safety net.
    if (expProgress > 0 && expSpan > 0) {
        if (!DrawAtlasBar(kExpBarBase, expProgress, expSpan, kVitalsBarX, kExpBarY))
            DrawBarFillQuantized(expBar, expProgress, expSpan, kVitalsBarSteps, kExpBg, kExpFill);
    }

    // Elemental mastery: FIXED /3000 scale (fdiv dbl_7EDAE8 @0x67A6FC — a constant,
    // not a global), drawn if val > 0 (cmp dword_168746C,0 / jle @0x67A6EE).
    const int chi = game::g_Client.VarGet(0x168746C); // dword_168746C @0x67A6E7
    if (chi > 0) {
        if (!DrawAtlasBar(kMasteryBarBase, chi, kMasteryMax, kVitalsBarX, kMasteryBarY))
            DrawBarFillQuantized(masteryBar, chi, kMasteryMax, kVitalsBarSteps, kMasteryBg, kMasteryFill);
    }

    // §4 talisman badge (HUD-09 partial) — fixed anchor (190,75), cf. DrawTalismanBadge().
    DrawTalismanBadge();
}

// =============================================================================
// §4 talisman badge — HUD-09 (wave W9, PARTIAL, structure corrected)
// Anchor: UI_GameHud_Render @0x67A787-0x67A826. REAL structure re-proven by disasm
// (the gap folder's was wrong, cf. GameHud.cpp banner item 6):
//   if (dword_16758A4 > 0)            { Sprite2D_Draw(unk_981A84, 190, 75); }   // jle 67A7A6
//   else if (10 <= g_TalismanSlot < 20                                          // 67A7A6-67A7B6
//            && dword_1674710[slot] ∈ [19051..19060] ∪ [19061..19070])
//                                     { Sprite2D_Draw(unk_9819F0, 190, 75); }   // SAME sprite
//   // otherwise: NOTHING is drawn
// BOTH id ranges draw the SAME sprite unk_9819F0: merged here into a single
// [19051..19070] test (a strict equivalent, not an over-simplification).
// TODO [anchor 0x67A826]: the FOLLOWING blocks (currency on unk_96644C background,
// 2x durability "%d/100" from dword_167325C, 42 steps via dbl_7BAFD8=42.0 — NOT 41 —
// bases 0x41B=1051/clamp 0x445=1093) are NOT guarded by dword_16758A4 and are left
// out of scope: their anchors derive from sprite widths (W(unk_96644C), W(unk_8F16A4))
// the IDB doesn't give statically (fields +108/+112 zero, same limit as layout_.frame),
// and the durability block requires g_SelectedInvItemId + MobDb_GetEntry(mITEM) +
// record[0xBC] ∉ {1Ch,1Fh,20h}. Inventing them would produce a wrong placement. The
// currency amount is still shown by DrawTextPass() (GameHud_Text.cpp) at its
// documented simplified anchor.
// =============================================================================
void GameHud::DrawTalismanBadge() {
    const HudRect badge{ kTalismanBadgeX, kTalismanBadgeY, kTalismanBadgeSz, kTalismanBadgeSz };

    // Branch `cmp dword_16758A4,0 / jle loc_67A7A6` @0x67A787: > 0 -> unk_981A84, then
    // `jmp loc_67A826` (the two branches are EXCLUSIVE).
    if (game::g_Client.VarGet(0x16758A4) > 0) {
        DrawFilledRect(badge, kTalismanActive);
        DrawBorder(badge, 1, kFrameBorder);
        return;
    }

    // Talisman branch: `cmp g_TalismanSlot,0Ah / jl` + `cmp …,14h / jge` @0x67A7A6-0x67A7B6.
    const int slot = game::g_Client.VarGet(0x1674760); // g_TalismanSlot 0x1674760
    if (slot < 10 || slot >= 20) return;               // otherwise: NOTHING (no "else" sprite)

    // dword_1674710[slot]: `cmp ds:dword_1674710[eax*4], 4A6Bh` @0x67A7BD (19051); ranges
    // [4A6Bh..4A74h] (19051-19060) and [4A75h..4A7Eh] (19061-19070) -> SAME sprite unk_9819F0.
    const int itemId = game::g_Client.VarGet(0x1674710 + static_cast<uint32_t>(slot) * 4);
    if (itemId < 19051 || itemId > 19070) return;

    DrawFilledRect(badge, kTalismanSlotted);
    DrawBorder(badge, 1, kFrameBorder);
}

// Fallback for when quickBarWindow_ could not be allocated (bad_alloc, cf.
// GameHud.cpp::Init()) — old placeholder rendering, unchanged. The normal path now
// goes through quickBarWindow_->Render() (see Render() in GameHud_Render.cpp,
// mission 2026-07-14).
void GameHud::DrawQuickSlotFrames() {
    // Bar background panel.
    DrawFilledRect(layout_.quickBar, kFrameBg);
    DrawBorder(layout_.quickBar, 1, kFrameBorder);

    for (int i = 0; i < kQuickSlotCount; ++i) {
        const HudRect& s = layout_.slots[static_cast<size_t>(i)];
        DrawFilledRect(s, kSlotBg);
        DrawBorder(s, 1, kSlotBorder);

        // Assignment marker (item/skill icon placeholder).
        if (!slots_[static_cast<size_t>(i)].empty()) {
            DrawFilledRect(HudRect{ s.x + 4, s.y + 4, s.w - 8, s.h - 8 },
                           kSlotFilled);
        }
    }
}

} // namespace ts2::ui

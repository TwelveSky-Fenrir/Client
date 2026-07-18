// UI/GameHud_Text.cpp — GameHud text-only sub-passes (split out of GameHud.cpp for
// size; same class, see GameHud.cpp for the file-family banner and EA history).
// Holds DrawTextPass (vitals PV/PM/currency/EXP/Mastery text), DrawDebugTimeOverlay
// (§17 GM-only debug overlay), DrawQuestMarkerPanel and DrawQuestMarkerText (§17
// quest marker callout, Quest_DrawTracker 0x510FC0).
#include "UI/GameHud.h"
#include "UI/GameHud_Internal.h" // shared constants/helpers (kTextColor/kTextDim, kMasteryMax, kBody*, RdBodyI32, ComputeExpProgress)
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
#include <cstring>
#include <string>

namespace ts2::ui {

namespace {
// --- Quest marker callout palette (§17, Quest_DrawTracker 0x510FC0) ----------
constexpr D3DCOLOR kQuestMarkerBg     = 0xD0202028u; // translucent background (same tint as §17 tracker)
constexpr D3DCOLOR kQuestMarkerBorder = 0xFF3A3A48u;
constexpr D3DCOLOR kQuestMarkerDone   = 0xFF60FF60u; // icon: objective complete (green)
constexpr D3DCOLOR kQuestMarkerGoing  = 0xFFFFDD66u; // icon: objective in progress (gold)
constexpr D3DCOLOR kQuestMarkerTitle  = 0xFFFFDD66u; // title (same tint as QuestTrackerWindow::kColTitle)

constexpr int kQuestMarkerIconSz = 24;
constexpr int kQuestMarkerPadX   = 10;
constexpr int kQuestMarkerPadY   = 8;
constexpr int kQuestMarkerLineH  = 16;
} // namespace

// Text pass (font drawn through its own ID3DXSprite -> separate batch).
// Takes NO parameters (wave W9): reads its own values, cf. GameHud.h — the HP
// clamp @0x67A499 set by DrawVitalsFrame() (GameHud_Vitals.cpp) must be SEEN by
// the "%d/%d" text.
void GameHud::DrawTextPass() {
    if (!font_.Ready()) return;
    char buf[64];

    // FAITHFUL sources for the HP/MP texts = players[0] (cf. GameHud.cpp banner §2
    // and DrawVitalsFrame): the binary pushes dword_1687370/dword_168736C
    // @0x67A4A3-0x67A4AA (HP) and dword_1687378/dword_1687374 @0x67A545-0x67A54B
    // (MP) into "%d/%d". Read AFTER DrawVitalsFrame()'s clamp -> a negative HP is
    // never shown.
    const game::PlayerEntity& p0 = game::g_World.Self();
    const int hp    = RdBodyI32(p0.body, kBodyHpCur); // dword_1687370 (body+292), post-clamp
    const int maxHp = RdBodyI32(p0.body, kBodyHpMax); // dword_168736C (body+288)
    const int mp    = RdBodyI32(p0.body, kBodyMpCur); // dword_1687378 (body+300)
    const int maxMp = RdBodyI32(p0.body, kBodyMpMax); // dword_1687374 (body+296)
    // Currency: g_Currency 0x1673180, read directly by the binary @0x67A87F.
    const int currency = game::g_World.self.currency;

    font_.BeginBatch(D3DXSPRITE_ALPHABLEND);

    // HUD-VIT-06(c) — the "Lv %d" label drawn here previously was an INVENTION: no
    // level display exists in the vitals block 0x67A3C0-0x67A8FA (only texts:
    // "%d/%d" HP @0x67A4B0, "%d/%d" MP @0x67A552, "%.3f" EXP @0x67A6A2, "%d/%d"
    // mastery @0x67A742, "%d" currency @0x67A885). Removed — the portrait stays a
    // bare frame.

    // "%d/%d" HP/MP values RIGHT-ALIGNED to the x=207 edge, as the binary does (and
    // as EXP/Mastery further below in this file). HP: x=0xCF-width, y=7
    // (UI_GameHud_Render 0x67A3C0 @0x67A4DC 'mov ecx,0CFh; sub ecx,eax' / push 7
    // @0x67A4C6). MP: x=0xCF-width, y=21 (@0x67A57E 'mov edx,0CFh; sub edx,ecx' /
    // push 15h @0x67A568). The old bar-centered rendering was unfaithful.
    auto rightAlignedLabel = [&](int cur, int mx, int y) {
        std::snprintf(buf, sizeof(buf), "%d/%d", cur, mx);
        const int tw = font_.MeasureText(buf);
        font_.DrawTextStyled(buf, layout_.frame.x + 207 - tw, layout_.frame.y + y,
                             kTextColor, gfx::kStyleShadow);
    };
    rightAlignedLabel(hp, maxHp, 7);  // HP: y=7  @0x67A4C6
    rightAlignedLabel(mp, maxMp, 21); // MP: y=21 @0x67A568

    // §1 EXP + Mastery text (mission W4-F2, UI_GameHud_Render 0x67A690-0x67A782).
    // Recomputed locally (idempotent, zero cost): DrawTextPass takes no
    // progress/span parameters. EXP = "%.3f" PERCENTAGE of progress*100/span (a3f
    // @0x67A6A2, dbl_7EDAF0=100.0), right-aligned to x=207 (0xCF-width @0x67A6CE),
    // y=35 (0x23 @0x67A6B8). Mastery = "%d/%d" (val, 3000), right-aligned x=207,
    // y=49 (0x31 @0x67A758).
    {
        const game::SelfState& s = game::g_World.self;
        int p = 0, span = 0;
        ComputeExpProgress(s.level, s.levelBonus, p, span);
        // The binary draws this text UNCONDITIONALLY (0x67A68A-0x67A6E2: the `jle`
        // @0x67A641 skips only the BAR and lands exactly on loc_67A68A = this text).
        // `if (span > 0)` is therefore a div/0 guard added on the C++ side, not a
        // binary guard; since span is now non-zero on both branches (cf.
        // GetRebirthExpSpan), it has no observable effect. Kept to never divide by zero.
        if (span > 0) {
            std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(p) * 100.0 / span);
            const int tw = font_.MeasureText(buf);
            font_.DrawTextStyled(buf, layout_.frame.x + 207 - tw, layout_.frame.y + 35,
                                 kTextColor, gfx::kStyleShadow);
        }
        const int chi = game::g_Client.VarGet(0x168746C);
        if (chi > 0) {
            std::snprintf(buf, sizeof(buf), "%d/%d", chi, kMasteryMax);
            const int tw = font_.MeasureText(buf);
            font_.DrawTextStyled(buf, layout_.frame.x + 207 - tw, layout_.frame.y + 49,
                                 kTextColor, gfx::kStyleShadow);
        }
    }

    // §2 currency (top-right, EA 0x67A839-0x67A8FA) — data already available
    // (game::g_World.self.currency, kept up to date by the network handlers) but
    // never displayed before this pass (mission 2026-07-14). Real anchor = right
    // below the minimap panel; here, lacking the width of the (not yet loaded)
    // minimap panel, anchored to the screen edge with a fixed margin — an assumed
    // simplification, never blocking.
    // HUD-VIT-06(b): BARE "%d" format, faithful to the binary (`push offset aD`
    // @0x67A885, aD = "%d" @0x7A9780). The "Gold: " prefix written here previously
    // exists in NO string in the binary (the context is carried by the background
    // sprite unk_96644C, not the text).
    std::snprintf(buf, sizeof(buf), "%d", currency);
    {
        const int tw = font_.MeasureText(buf);
        font_.DrawTextStyled(buf, screenW_ - tw - 8, 4, kTextColor, gfx::kStyleShadow);
    }

    // Hotkey number in each quickslot (1..9 then 0) — ONLY as a fallback
    // (quickBarWindow_ draws its own key numbers on the normal path, cf. Render()
    // in GameHud_Render.cpp; avoids double-drawing).
    if (!quickBarWindow_) {
        for (int i = 0; i < kQuickSlotCount; ++i) {
            const HudRect& s = layout_.slots[static_cast<size_t>(i)];
            const int key = (i + 1) % 10; // slot 10 -> key '0'
            std::snprintf(buf, sizeof(buf), "%d", key);
            font_.DrawTextStyled(buf, s.x + 3, s.y + 2, kTextDim, gfx::kStyleShadow);
        }
    }

    font_.EndBatch();
}

// §17 GM-only debug time overlay — EA 0x686942 (inside UI_GameHud_Render, NOT in
// Quest_DrawTracker 0x6868AB as the doc implied, cf. GameHud.cpp banner). AUTONOMOUS
// font batch (own BeginBatch/EndBatch): only opens the batch when content will
// actually be drawn, to stay a silent no-op off GM accounts (same policy as
// buffPanel_/chatWindow_, autonomous widgets below).
void GameHud::DrawDebugTimeOverlay() {
    // EXACT binary condition @0x6868e8-0x6868f8: `dword_1676108 > 0 && g_GmAuthLevel > 0`.
    // dword_1676108 is already kept up to date by ApplySetGameVar case 98 (Net/GameVarDispatch.cpp).
    if (game::g_Client.VarGet(0x1676108) <= 0) return;
    if (net::g_GmAuthLevel == 0) return;
    if (!font_.Ready()) return;

    // Literal format "NowTime : %d / %d %d:%d %s" (aNowtimeDDDDS @0x7baf78). The 4
    // integers = month+1/day/hour/minute decoded from dword_1676108 by
    // ApplySetGameVar case 98 (dword_167610C/1676110/1676114/1676118). The %s
    // (byte_167611C) stays empty as long as Crt_StringInit (0x75CAB0) is not
    // ported (TODO(ui) already documented in GameVarDispatch.cpp) — an honest
    // degradation, not invented text.
    char buf[128];
    const auto& dayStr = game::g_Client.Blob(0x167611C, 64);
    std::snprintf(buf, sizeof(buf), "NowTime : %d / %d %d:%d %s",
                  game::g_Client.VarGet(0x167610C),
                  game::g_Client.VarGet(0x1676110),
                  game::g_Client.VarGet(0x1676114),
                  game::g_Client.VarGet(0x1676118),
                  reinterpret_cast<const char*>(dayStr.data()));

    // Fixed anchor (10,150) @0x686934/0x68692f (push 0Ah/push 96h before
    // UI_DrawNumberValue 0x53FCC0, which relays to UI_DrawText(a1=text, x, y,
    // color, style)). Original color = ColorTable_GetColor(dword_84DF20, 3) (a
    // color table not modeled client-side); kTextColor + shadow = same convention
    // as the rest of the HUD (DrawTextPass above), not a blocking visual invention.
    font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
    font_.DrawTextStyled(buf, 10, 150, kTextColor, gfx::kStyleShadow);
    font_.EndBatch();
}

// =============================================================================
// §17 quest marker callout — Quest_DrawTracker 0x510FC0 (mission 2026-07-14, see
// GameHud.cpp "QUEST MARKER CALLOUT WIRING" banner). SINGLE, faithful gate:
// `questMarker_.active` (== *(this+51576) != 0 in Quest_DrawTracker).
// =============================================================================

// Sprite pass: frame + icon dot (colored fallback, cf. GameHud.cpp banner for the
// note on the real mNPC/g_AssetMgr_UiAtlasSlots icon, not modeled).
void GameHud::DrawQuestMarkerPanel() {
    if (!questMarker_.active) return; // EA 0x510fd9: head guard of Quest_DrawTracker

    const HudRect& r = layout_.questMarker;
    DrawFilledRect(r, kQuestMarkerBg);
    DrawBorder(r, 1, kQuestMarkerBorder);

    // lastObjectiveState==1 <=> "objective complete" branch of
    // Quest_UpdateMarkerTimer (EA 0x510e13, plays Snd3D_PlayScaledVolume); any
    // other non-zero value <=> "new objective in progress" branch (EA 0x510ecc,
    // markerVariant = Rng_Next()%3+1).
    const bool complete = questMarker_.lastObjectiveState == 1;
    const HudRect icon{ r.x + 4, r.y + kQuestMarkerPadY, kQuestMarkerIconSz, kQuestMarkerIconSz };
    DrawFilledRect(icon, complete ? kQuestMarkerDone : kQuestMarkerGoing);
    DrawBorder(icon, 1, kQuestMarkerBorder);
}

// Standalone text pass (same policy as DrawDebugTimeOverlay above): title + raw
// zone/NPC/target identifiers (game::QuestProgressState + game::QuestStepRecord via
// LookupQuestStep, an injectable resolver — nullptr by default, cf.
// Game/QuestSystem.h), NO invented variant text (the StrTable003/mZONENPCINFO table
// that sources the real "%s (%d,%d)"/"%s!" text of Quest_DrawTracker is out of scope,
// cf. GameHud.cpp banner).
void GameHud::DrawQuestMarkerText() {
    if (!questMarker_.active) return;
    if (!font_.Ready()) return;

    const game::QuestProgressState& progress = game::g_QuestProgress;
    const bool complete = questMarker_.lastObjectiveState == 1;
    // Same record selection as the binary (EA 0x511048/0x5110a8/0x5110d1):
    // npcQuestId+1 if "objective complete" (the NEXT step already resolved),
    // npcQuestId otherwise.
    const int npcQuestId = complete ? progress.npcQuestId + 1 : progress.npcQuestId;
    const game::QuestStepRecord* step = game::LookupQuestStep(progress.zoneId, npcQuestId);

    const HudRect& r = layout_.questMarker;
    char buf[128];

    font_.BeginBatch(D3DXSPRITE_ALPHABLEND);

    int ty = r.y + kQuestMarkerPadY;
    const char* title = complete ? "Quete : objectif rempli !" : "Quete : nouvel objectif";
    font_.DrawTextStyled(title, r.x + kQuestMarkerIconSz + kQuestMarkerPadX + 4, ty,
                         kQuestMarkerTitle, gfx::kStyleShadow);
    ty += kQuestMarkerLineH;

    // Zone/NPC — same name resolution as UI/QuestTrackerWindow.cpp::BuildLayout
    // (StrTable003_Get via game::g_Strings.zoneNames, falls back to the numeric id).
    const char* zoneName = game::g_Strings.zoneNames.Get(progress.zoneId);
    if (zoneName && zoneName[0] != '\0')
        std::snprintf(buf, sizeof(buf), "%s - Quete NPC #%d", zoneName, progress.npcQuestId);
    else
        std::snprintf(buf, sizeof(buf), "Zone #%d - Quete NPC #%d", progress.zoneId, progress.npcQuestId);
    font_.DrawTextStyled(buf, r.x + kQuestMarkerPadX, ty, kTextColor, gfx::kStyleShadow);
    ty += kQuestMarkerLineH;

    if (step) {
        std::snprintf(buf, sizeof(buf), "Cible #%u (%d/%u)",
                      step->targetId, progress.objectiveProgress, step->required);
    } else {
        // QuestStepLookup not wired (injectable resolver defaults to null, cf.
        // banner above): honest fallback to the raw identifier, no invented text.
        std::snprintf(buf, sizeof(buf), "Cible : PNJ mQUEST #%d", npcQuestId);
    }
    font_.DrawTextStyled(buf, r.x + kQuestMarkerPadX, ty, kTextColor, gfx::kStyleShadow);

    font_.EndBatch();
}

} // namespace ts2::ui

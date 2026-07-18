// UI/GameHud_Render.cpp — GameHud top-level render/input entry points (split out of
// GameHud.cpp for size; same class, see GameHud.cpp for the file-family banner and
// EA history). Holds DrawMenuButtons (§15 menu button row), Render
// (cGameHud_Render 0x64A900) and OnMouseDown (cGameHud_OnMouseDown 0x62B080).
#include "UI/GameHud.h"
#include "UI/GameHud_Internal.h" // shared constants (kAllyFrameBg/Brd, kAllyIconOnline/Offline, kAllyNoData, kAllyNameCol, kAllianceIconSize, kAllianceBarH, kHpBg/kHpFill, kBarBorder, kTextColor, kTextDim)
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

// =============================================================================
// §7 locked-target plates + §15 row of menu buttons (mission W4-F2) — file-local
// helpers (GameHud.h stays read-only: no method/field added).
// =============================================================================
namespace {

// --- §7 target plates (UI_GameHud_Render 0x67B0D3 / 0x67B436) ---------------
// Name registers = 13-byte blobs 0x167468A (plate A) / 0x1674697 (plate B), written
// by Pkt SC 0x44 (Net/GameHandlers_ChatSocial.cpp) and already read by
// Scene/SceneManager.cpp::readReqName (same pattern replicated here). Display gated
// on a non-empty name (`Crt_Strcmp(&name,"") != 0` @0x67B0D3). Resolution BY NAME in
// g_World.players (the binary scans g_EntityArray 0x1687234, stride 0x38C, name at
// entity+72 @0x67B115-0x67B171; here cross-referenced with the players[] mirror,
// same method as BuildAllianceFrames). FAITHFUL LIMIT: maxHp of a REMOTE entity is
// not modeled (PlayerEntity has no maxHp) -> gauge grayed rather than an invented ratio.
constexpr int kTargetPlateY0   = 105; // panel at (0,105), above the §8 alliance rows (y=155)
constexpr int kTargetPlateW    = 204;
constexpr int kTargetPlateH    = 48;
constexpr int kTargetPlateStep = 50;  // plates A/B stack when both are active

struct TargetPlate {
    bool        active   = false; // name register non-empty (gate 0x67B0D3)
    bool        resolved = false; // entity found BY NAME in g_World.players
    std::string name;
    int         hp = 0, hpMax = 0;
    bool        hpMaxKnown = false; // known maxHp (self only, cf. §8 alliance)
};

// Replica of Scene/SceneManager.cpp::readReqName (13-byte blob, read up to NUL).
std::string ReadTargetName(uint32_t addr) {
    const auto& blob = game::g_Client.Blob(addr, 13);
    size_t len = 0;
    while (len < blob.size() && blob[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(blob.data()), len);
}

// "Resolved current target" accessor (requested by task W4-F2).
TargetPlate ResolveTargetPlate(uint32_t nameAddr) {
    TargetPlate tp;
    tp.name   = ReadTargetName(nameAddr);
    tp.active = !tp.name.empty();
    if (!tp.active) return tp;

    const auto& players = game::g_World.players;
    for (size_t pi = 0; pi < players.size(); ++pi) {
        const game::PlayerEntity& p = players[pi];
        if (!p.active || p.name != tp.name) continue;
        tp.resolved = true;
        if (pi == 0) {
            // players[0] = self (EntityManager convention): real HP/maxHP via StatEngine.
            const game::SelfState& self = game::g_World.self;
            tp.hp = self.hp; tp.hpMax = self.maxHp; tp.hpMaxKnown = true;
        } else {
            tp.hp = p.hp; // real current HP; remote maxHp not modeled -> hpMaxKnown=false
        }
        break;
    }
    return tp;
}

// --- §15 menu buttons (HUD-03) — colored fallback -----------------------------
// The binary's 3 states are STANDALONE Sprite2D objects (unk_944C60 normal /
// unk_944CF4 hover / unk_944EB0 active), NOT elements of g_AssetMgr_UiAtlasSlots:
// their filename@+4 field is filled at runtime, no static literal links them to a
// .IMG (same limitation as the vitals frame before it was resolved). Colored dot
// fallback, cf. DrawMenuButtons() below.
constexpr D3DCOLOR kMenuBtnBg     = 0xC0242430u;
constexpr D3DCOLOR kMenuBtnBorder = 0xFF50505Eu;

// --- §15 row of menu buttons (UI_GameHud_Render 0x685177) ----------------
// Each Sprite2D icon is drawn at (quickbar anchor + dx, + dy), where the anchor =
// (this[0],this[1]) = corner of the quickbar background (EA 0x684CB0/0x684CBC, =
// layout_.quickBar +4). The click calls sub_4C1110(0) (= Util_SetClampedU8Field,
// toggling a U8 flag this+flagOff = open/close a window, target NOT statically
// identified). dx/dy/flag offsets taken from the §15 doc. Button sizes not
// statically readable (unloaded .npk skin icons) -> estimated 24x16 (same limit as §1/§14).
struct MenuBtn { int dx, dy, flagOff; };
const MenuBtn kMenuButtons[] = {
    {   0, -17, 124 }, {  25, -17, 396 }, {  59, -16, 452 }, {  84, -16, 448 },
    { 109, -16, 444 }, { 134, -16, 440 }, { 159, -16, 436 }, { 184, -16, 428 },
    { 234, -17, 420 }, { 284, -17, 412 }, { 334, -17, 384 }, { 359, -17, 388 },
    { 384, -17, 392 }, { 309, -17, 408 }, { 409, -17, 400 }, { 434, -17, 404 },
    { 458,   2, 432 },
};
constexpr int kMenuBtnW = 24;
constexpr int kMenuBtnH = 16;

} // namespace

// =============================================================================
// §15 row of menu buttons — HUD-03 (wave W9)
// Anchor: UI_GameHud_Render @0x685177-0x685229 (pattern re-proven by fresh disasm this
// day), repeated ~17 times (next button: `add ecx, 1B2h` = +434 @0x68524B ->
// unk_94A724/94A7B8/94A84C). BEFORE this pass, kMenuButtons was read ONLY by
// OnMouseDown: the buttons were clickable but NEVER drawn (exhaustive grep on src/:
// 2 occurrences, none in rendering).
//
// Binary pattern per button (this = quickbar background, this[0]/this[1] = its corner):
//   if (Sprite2D_HitTest(&unk_944C60, this[0], this[1]-11h, curX, curY))   // 0x68517D
//        Util_SetClampedU8Field(dword_8E714C, 0);                          // 0x68518D
//   if (*(this+7Ch))  Sprite2D_Draw(&unk_944EB0, ...);   // active  0x685198 -> 0x685224
//   else if (hit)     Sprite2D_Draw(&unk_944CF4, ...);   // hover   0x6851C1 -> 0x6851E5
//   else              Sprite2D_Draw(&unk_944C60, ...);   // normal          0x685207
//
// TWO of the three states are not reproducible here, left as explicit TODOs RATHER
// THAN GUESSED (rule #8):
//  - TODO [anchor 0x685198]: the "active" state tests the binary HUD object's U8 flag
//    `this+flagOff` (e.g. this+7Ch=124 for the 1st button) — this object is NOT
//    modeled client-side (GameHud shares no memory layout with it) and the real
//    target of Util_SetClampedU8Field(dword_8E714C, 0) is not statically identified.
//    All buttons are therefore rendered in the "normal" state.
//  - TODO [anchor 0x6851C1]: the "hover" state needs the cursor position. Render()
//    has no access to it (ctx.cursorX/cursorY stay (-1,-1): no WM_MOUSEMOVE is
//    routed from Scene/SceneManager.cpp to hud_, same limit as quickBarWindow_ below).
//
// ANCHOR: (layout_.quickBar.x+4, layout_.quickBar.y+4) — STRICTLY the same expression
// as OnMouseDown() below, to guarantee draw ≡ hit-test. DOCUMENTED DEVIATION vs. the
// binary: the real anchor @0x684CA8-0x684D08 derives from the quickbar background
// sprite unk_940388 (`nWidth/2 - W/2`, `nHeight - H`), whose dimensions are not
// statically readable (fields +108/+112 zero in the IDB); layout_.quickBar is already
// an estimate derived from the real pitch (cf. InitLayout). Reusing the same
// expression on both sides is the only way to stay consistent without inventing a
// sprite width.
// =============================================================================
void GameHud::DrawMenuButtons() {
    const int anchorX = layout_.quickBar.x + 4; // this[0] (cf. EA 0x684CB0 and OnMouseDown)
    const int anchorY = layout_.quickBar.y + 4; // this[1] (cf. EA 0x684CBC and OnMouseDown)

    for (const MenuBtn& mb : kMenuButtons) {
        const HudRect r{ anchorX + mb.dx, anchorY + mb.dy, kMenuBtnW, kMenuBtnH };
        // "Normal" state (unk_944C60) for all: cf. the 2 TODOs in the banner above.
        // Colored-rect fallback — the 3 state sprites are STANDALONE Sprite2D objects
        // whose filename@+4 field is filled at runtime (no static literal links them to
        // a .IMG), unlike the vitals bars which index g_AssetMgr_UiAtlasSlots.
        DrawFilledRect(r, kMenuBtnBg);
        DrawBorder(r, 1, kMenuBtnBorder);
    }
}

// =============================================================================
// Render — cGameHud_Render 0x64A900
// =============================================================================
void GameHud::Render() {
    if (!visible_ || !device_ || !sprite_.Ready()) return;

    // (wave W9) The `const game::SelfState& self` alias that used to live here no
    // longer exists: its only consumer was the DrawTextPass(self.hp, self.maxHp, …)
    // call, now parameterless — and those fields were precisely the WRONG source
    // (cf. GameHud.cpp banner §2). Each sub-pass now reads its own source directly.

    // Shared UiContext (cf. UI/UIManager.h) — built locally, NOT dependent on a
    // registered UIManager::Instance() (GameHud is not a Dialog). Consumed by
    // quickBarWindow_ below (mission 2026-07-14).
    UiContext ctx;
    ctx.renderer    = rendererPtr_; // cf. UI/GameHud.h::rendererPtr_ (audit 2026-07-14)
    ctx.sprites     = &sprite_;
    ctx.font        = &font_;
    ctx.whiteTex    = white_;
    ctx.screenW     = screenW_;
    ctx.screenH     = screenH_;
    ctx.gameTimeSec = game::g_World.gameTimeSec;

    // §17 quest marker callout (mission 2026-07-14, see GameHud.cpp "QUEST MARKER
    // CALLOUT WIRING" banner): ticks state BEFORE the render passes (same inputs as
    // Scene/SceneManager.cpp::host.UpdateQuestMarkerTimer -
    // game::g_QuestProgress + game::g_World.gameTimeSec, isArenaZone=false for lack
    // of a modeled Map_IsArenaZone here as there), cf. GameHud.h::questMarker_ for
    // the note on this instance being OWNED by GameHud (no sharing possible with
    // the SceneManager.cpp local static, a file deliberately not modified).
    game::Quest_UpdateMarkerTimer(questMarker_, game::g_QuestProgress, game::g_World.gameTimeSec,
                                  /*isArenaZone=*/false, /*warehouseTargetMatches=*/nullptr,
                                  /*playMarkerSound=*/nullptr);

    // Pass 1: flat sprites (frame + bars + §14 quickbar + §12 minimap + §8
    // alliance/party frames + §17 quest marker callout).
    if (SUCCEEDED(sprite_.Begin(D3DXSPRITE_ALPHABLEND))) {
        DrawVitalsFrame();
        if (quickBarWindow_) {
            ctx.phase = UiPhase::Panels;
            // cursorX/cursorY default to (-1,-1): SceneManager doesn't route any
            // WM_MOUSEMOVE event to GameHud today (cf. precise TODO in the mission
            // report) -> no hover highlight.
            quickBarWindow_->Render(ctx, slots_);
        } else {
            DrawQuickSlotFrames(); // fallback if allocation failed (cf. GameHud.cpp::Init())
        }
        // §15 row of menu buttons (HUD-03, wave W9) — drawn AFTER the quickbar
        // (the buttons are anchored on ITS corner and overflow above it, dy=-17),
        // faithful to the binary's order (block 0x685177+ after the slot loop
        // 0x684DC9-0x685155). Render-side counterpart of OnMouseDown()'s hit-test:
        // without this call the ~17 buttons stayed clickable but invisible.
        DrawMenuButtons();
        minimap_.DrawPanels(sprite_, white_);
        // §7 x2 locked-target plates (mission W4-F2, UI_GameHud_Render 0x67B0D3 /
        // 0x67B436) — info panels at (0,105+), drawn BEFORE the §8 alliance rows
        // (those at y=155). Same degradation policy as §8: an unmodeled remote
        // target's maxHp -> gauge grayed, never an invented ratio.
        {
            const TargetPlate plates[2] = {
                ResolveTargetPlate(0x167468A), // plate A (0x67B0D3)
                ResolveTargetPlate(0x1674697), // plate B (0x67B436)
            };
            int prow = 0;
            for (int pi = 0; pi < 2; ++pi) {
                const TargetPlate& tp = plates[pi];
                if (!tp.active) continue; // gate `Crt_Strcmp(name,"") != 0` @0x67B0D3
                const int py = kTargetPlateY0 + prow * kTargetPlateStep;
                const HudRect frame{ 0, py, kTargetPlateW, kTargetPlateH };
                DrawFilledRect(frame, kAllyFrameBg);
                DrawBorder(frame, 1, kAllyFrameBrd);
                // Presence dot (the binary distinguishes offline unk_923758 /
                // max-level unk_9464A8 / normal unk_9236C4 icons; reduced here to
                // resolved/not-found).
                const HudRect icon{ 4, py + 4, kAllianceIconSize, kAllianceIconSize };
                DrawFilledRect(icon, tp.resolved ? kAllyIconOnline : kAllyIconOffline);
                DrawBorder(icon, 1, kAllyFrameBrd);
                // HP mini-bar only (36 steps, frames 520-556 @ (5,129) local in the
                // binary) — NO MP bar (unlike the §8 alliance frames).
                const HudRect hpBar{ 5, py + 24, kTargetPlateW - 10, kAllianceBarH };
                if (tp.resolved && tp.hpMaxKnown) {
                    DrawBarFill(hpBar, tp.hp, tp.hpMax, kHpBg, kHpFill);
                } else {
                    DrawFilledRect(hpBar, kAllyNoData);
                    DrawBorder(hpBar, 1, kBarBorder);
                }
                ++prow;
            }
        }
        // §8 alliance/party frames (mission 2026-07-14, see GameHud.cpp banner) —
        // rebuilt every frame (negligible cost: at most 5 slots x linear search in
        // g_World.players), same policy as UI/PartyWindow.cpp.
        DrawAllianceFramePanels(BuildAllianceFrames());
        DrawQuestMarkerPanel(); // §17 callout — no-op if questMarker_.active is false
        sprite_.End();
    }

    // Pass 2: text (font batch, separate from the sprite batch) — HP/PM values + §2 currency.
    // DrawTextPass() now reads its own sources (players[0], cf. banner §2 in
    // GameHud.cpp): the HP clamp @0x67A499 set by DrawVitalsFrame() above MUST be
    // seen by the text, which a snapshot passed as a parameter from here would prevent.
    DrawTextPass();

    // Pass 2bis: real-quickbar text (hotkey numbers, owned quantities, last-click
    // feedback message) — second UiContext sub-pass of the same widget as pass 1 above.
    if (quickBarWindow_ && font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        ctx.phase = UiPhase::Text;
        quickBarWindow_->Render(ctx, slots_);
        font_.EndBatch();
    }

    // Pass 3: minimap text (toggle button + zone name/coordinates in large mode) —
    // separate batch so as not to touch DrawTextPass()'s signature above.
    if (font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        minimap_.DrawText(font_);
        font_.EndBatch();
    }

    // Pass 3bis: alliance/party frame text (§8, mission 2026-07-14) — names + real
    // HP/PM values. Independent rebuild from pass 1 (same policy as
    // UI/PartyWindow.cpp: identical result within the same frame, no ordering
    // dependency between the two phases).
    if (font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        DrawAllianceFrameText(BuildAllianceFrames());
        font_.EndBatch();
    }

    // Pass 3quinquies: target plate text (§7, mission W4-F2) — name (centered x=75
    // @0x67B294) + real HP (with max if known) + plate B counter (VarGet
    // 0x16746A4 @0x67B...). Independent rebuild from pass 1 (identical result).
    if (font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        const TargetPlate plates[2] = {
            ResolveTargetPlate(0x167468A),
            ResolveTargetPlate(0x1674697),
        };
        int prow = 0;
        char b[64];
        for (int pi = 0; pi < 2; ++pi) {
            const TargetPlate& tp = plates[pi];
            if (!tp.active) continue;
            const int py = kTargetPlateY0 + prow * kTargetPlateStep;
            font_.DrawTextStyled(tp.name.c_str(), 75, py + 6,
                                 tp.resolved ? kAllyNameCol : kTextDim, gfx::kStyleShadow);
            if (tp.resolved) {
                if (tp.hpMaxKnown) std::snprintf(b, sizeof(b), "%d/%d", tp.hp, tp.hpMax);
                else               std::snprintf(b, sizeof(b), "%d", tp.hp); // remote max not modeled
                font_.DrawTextStyled(b, 8, py + 24, kTextColor, gfx::kStyleShadow);
            }
            if (pi == 1) {
                // Plate B: bottom-right counter, shown even with no active target (@0x67B436).
                std::snprintf(b, sizeof(b), "%d", game::g_Client.VarGet(0x16746A4));
                const int tw = font_.MeasureText(b);
                font_.DrawTextStyled(b, kTargetPlateW - tw - 8, py + kTargetPlateH - 14,
                                     kTextDim, gfx::kStyleShadow);
            }
            ++prow;
        }
        font_.EndBatch();
    }

    // Pass 3ter: GM-only debug time overlay (§17, EA 0x686942, mission 2026-07-14,
    // see DrawDebugTimeOverlay() in GameHud_Text.cpp). Silent no-op off GM accounts.
    DrawDebugTimeOverlay();

    // Pass 3quater: quest marker callout text (§17, Quest_DrawTracker 0x510FC0,
    // mission 2026-07-14). Silent no-op if questMarker_.active is false.
    DrawQuestMarkerText();

    // Cast indicator (§16, EA 0x6865BF+, `dword_1685E74[g_LocalElement] != 0`) —
    // wired mission 2026-07-14: BuffStatusPanel::casting_ already existed (pulsing
    // icon, 8 frames, cycle Crt_ftol(g_GameTimeSec*16)%8 faithful to the EA) but
    // SetCasting() was NEVER called anywhere -> the indicator stayed permanently
    // off. The state data IS already modeled faithfully on the
    // Game/ActionStateMachine.h side (CharActionState, terminal switch of
    // Char_UpdateAnimationFrame 0x571880/0x5727BF, EXACT binary offsets) and kept
    // up to date every frame for SELF by
    // SceneManager::host.UpdateEntityAnimFrame (idx==0) -> game::g_World.Self().anim.state
    // (entity+244, the SAME memory word as CharAnimState::state, cf.
    // ActionStateMachine.h banner). Semantics faithful to the IDA comment ("casting
    // /channeling an elemental skill"): CastSlot0/1/2 (0x05-0x07,
    // Char_CastAnimTick_57*, skill windup) = casting, Channel (0x28,
    // Char_TickChannelState 0x57A700) = channeling/hold.
    const int32_t selfActionState = game::g_World.Self().anim.state;
    const bool isCasting =
        selfActionState == static_cast<int32_t>(game::CharActionState::CastSlot0) ||
        selfActionState == static_cast<int32_t>(game::CharActionState::CastSlot1) ||
        selfActionState == static_cast<int32_t>(game::CharActionState::CastSlot2) ||
        selfActionState == static_cast<int32_t>(game::CharActionState::Channel);
    buffPanel_.SetCasting(isCasting);

    // Pass 4: buff grid (§9) + bottom-right status panel (§16). AUTONOMOUS widget:
    // manages its own sprite_.Begin()/End() and its own font batch (cf.
    // BuffStatusPanel::Render), so called outside passes 1-3.
    buffPanel_.Render();

    // Pass 5: chat & system message window (§13, mission 2026-07-14). AUTONOMOUS
    // widget (manages its own lazy internal ID3DXSprite + its own font batch, cf.
    // ChatWindow::Render): shares font_ (a parameter, not an owned resource) but
    // NOT sprite_/white_ (cf. GameHud.h banner). Resyncs from the shared message
    // log game::g_Client.msg every frame — idempotent, only republishes new lines
    // (cf. ChatWindow::SyncFromMessageLog).
    chatWindow_.Tick(game::g_World.gameTimeSec);
    chatWindow_.SyncFromMessageLog(game::g_Client.msg);
    chatWindow_.Render(sprite_, font_);
}

// =============================================================================
// OnMouseDown — cGameHud_OnMouseDown 0x62B080
// =============================================================================
bool GameHud::OnMouseDown(int x, int y) {
    lastClickedSlot_ = -1;
    if (!visible_) return false;

    // Chat window (§13, mission 2026-07-14): clickable channel tabs (§13b). Tested
    // first ("first consumer wins"); no real overlap with the quickbar
    // (bottom-left vs. bottom-center).
    if (chatWindow_.OnMouseDown(x, y)) {
        TS2_LOG("GameHud : clic onglet de chat (%d,%d), canal=%d",
                x, y, static_cast<int>(chatWindow_.Channel()));
        return true;
    }

    // Real quickbar (§14, mission 2026-07-14). ASSUMED FIDELITY GAP: the binary
    // distinguishes mouse-down (arms a latch, cf. UI_ConsumableBar_OnClick
    // 0x68E3C0) from mouse-up (triggers the action, cf. UI_ConsumableBar_ProcInput
    // 0x68E5A0); the existing SceneManager::OnMouseUp does NOT route to hud_ in the
    // InGame scene today (only OnMouseDown does, cf. precise TODO in the mission
    // report) — so OnMouseDown()+OnClick() of the widget are chained in the same
    // call to stay usable without this wiring, at the cost of losing the
    // click/drag-start distinction.
    if (quickBarWindow_) {
        if (quickBarWindow_->OnMouseDown(x, y, slots_)) {
            quickBarWindow_->OnClick(x, y, slots_);
            lastClickedSlot_ = quickBarWindow_->LastDecision().slotIndex;
            TS2_LOG("GameHud : clic quickslot %d (%s)",
                    lastClickedSlot_, quickBarWindow_->LastMessage().c_str());
            return true;
        }
    } else {
        // Historical fallback (quickBarWindow_ not allocated, cf.
        // GameHud.cpp::Init()): old raw hit-test with no trigger logic.
        for (int i = 0; i < kQuickSlotCount; ++i) {
            if (layout_.slots[static_cast<size_t>(i)].Contains(x, y)) {
                lastClickedSlot_ = i;
                const QuickSlot& s = slots_[static_cast<size_t>(i)];
                TS2_LOG("GameHud : clic quickslot %d (type=%u ref=%u)",
                        i, static_cast<unsigned>(s.type), s.refId);
                return true;
            }
        }
    }

    // Minimap (§12): size toggle button + generic panel click.
    if (minimap_.OnMouseDown(x, y)) {
        TS2_LOG("GameHud : clic mini-carte (%d,%d), grande=%d", x, y, minimap_.BigMode());
        return true;
    }

    // Buff grid (§9) + bottom-right panel (§16): consumed if the click falls
    // within either zone (generic tooltip not modeled, cf.
    // BuffStatusPanel::OnMouseDown).
    if (buffPanel_.OnMouseDown(x, y))
        return true;

    // §7 target plates (mission W4-F2): pure info panel (no sub-hit-test),
    // consumed if the click falls within the active plates' zone (first consumer
    // wins, same policy as §8 alliance).
    {
        const TargetPlate tpA = ResolveTargetPlate(0x167468A);
        const TargetPlate tpB = ResolveTargetPlate(0x1674697);
        const int nActive = (tpA.active ? 1 : 0) + (tpB.active ? 1 : 0);
        if (nActive > 0) {
            const HudRect area{ 0, kTargetPlateY0, kTargetPlateW, kTargetPlateStep * nActive };
            if (area.Contains(x, y))
                return true;
        }
    }

    // Alliance/party frames (§8, mission 2026-07-14): consumed if the click falls
    // within the currently populated zone (no per-row sub-hit-test, pure info
    // panel — same policy as UI/PartyWindow.cpp/UI/QuestTrackerWindow.h).
    if (AllianceFramesContains(x, y))
        return true;

    // §15 row of menu buttons (mission W4-F2, UI_GameHud_Render 0x685177): ~17
    // icons around the quickbar, anchored on the quickbar background (this[0]/
    // this[1], EA 0x684CB0/0x684CBC = layout_.quickBar corner + 4). Real action =
    // sub_4C1110(0) (toggles a U8 flag this+flagOff -> opens an unidentified
    // window): here we CONSUME + log, without opening a GameWindows entry
    // (target not statically proven).
    {
        const int anchorX = layout_.quickBar.x + 4; // this[0] (0x684CB0)
        const int anchorY = layout_.quickBar.y + 4; // this[1] (0x684CBC)
        for (const MenuBtn& mb : kMenuButtons) {
            const HudRect r{ anchorX + mb.dx, anchorY + mb.dy, kMenuBtnW, kMenuBtnH };
            if (r.Contains(x, y)) {
                lastClickedSlot_ = -1;
                TS2_LOG("GameHud : clic bouton de menu (flag+%d) a (%d,%d)", mb.flagOff, x, y);
                return true; // sub_4C1110(0): toggle flag (target window not identified)
            }
        }
    }

    // Click on the vitals frame or the quickslot panel: consumed (blocks the
    // click from reaching the 3D scene behind the HUD).
    if (layout_.frame.Contains(x, y) || layout_.quickBar.Contains(x, y))
        return true;

    return false;
}

} // namespace ts2::ui

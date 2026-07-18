// UI/GameHud.h — main in-game HUD (cGameHud).
//
// SKELETON REWRITE, faithful in spirit to the TwelveSky2 binary:
//   cGameHud_InitLayout  0x62A5B0  -> Init()/InitLayout()  (anchor + derived rects)
//   cGameHud_Render      0x64A900  -> Render()             (~16 KB original)
//   cGameHud_OnMouseDown 0x62B080  -> OnMouseDown()         (left-click dispatcher)
//   QuickSlot singleton  dword_18392C0 / UI_QuickSlot_AssignHotkey 0x5bdf00
//
// The original cGameHud is the big "character" window (inventory/equipment). Here we
// implement the ALWAYS-VISIBLE part of the HUD: HP/MP bars (read from
// game::g_World.self), the quickslot bar, and a small portrait frame. Drawn via
// gfx::SpriteBatch (tinted flat rects) + gfx::Font (labels).
//
// See Docs/TS2_CLIENT_SHELL.md §2.3 (HUD) and §4 (quickslots DIK 0x02..0x0B).
//
// COMPLETENESS AUDIT vs Docs/TS2_UI_GAMEHUD_RENDER.md (mission 2026-07-14, idaTs2
// already decompiled in an earlier session — this file does NOT redo RE, it consumes
// the existing doc + the code's actual state): full table and deltas documented in
// the mission report (see agent's reply). Two widgets already written but NEVER
// instantiated anywhere in ClientSource were wired here in this pass:
//   - UI/ChatWindow.h (§13 chat & system message window, bottom-left) — the data
//     pipeline already existed (game::g_Client.msg, fed by network handlers) but
//     nothing displayed it.
//   - UI/ConsumableBarWindow.h (§14 quickbar, pixel counterpart of
//     Game/ConsumableBarLogic.h) — replaces the old placeholder-rect fill of
//     DrawQuickSlotFrames() with rendering driven by real inventory (stock count,
//     "missing item" tint), kept as a fallback if instantiation fails. See the
//     UI/GameHud.cpp banner for anchor/limitation detail (no mouse-up or keyboard
//     event routed from SceneManager in this pass — precise TODO documented in the
//     .cpp).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h" // atlas cache for the 4 vitals bars (wave W9, cf. vitalsAtlasCache_)
#include "UI/MinimapWidget.h"
#include "UI/BuffStatusPanel.h"
#include "UI/ChatWindow.h"
#include "Game/ComboPickupTick.h" // game::QuestMarkerState/Quest_UpdateMarkerTimer (§17 callout, mission 2026-07-14)

namespace ts2::ui {

// Defined in UI/ConsumableBarWindow.h, which ITSELF includes UI/GameHud.h (for
// ts2::ui::QuickSlot/kQuickSlotCount): including it directly here would create a
// header cycle. Forward-declare + std::unique_ptr, full implementation in
// GameHud.cpp (the only translation unit that needs the complete type). See the
// banner above and in GameHud.cpp.
class ConsumableBarWindow;

// Number of quickslots in the main bar: keys 1..0 = DIK scancodes 0x02..0x0B
// (Docs/TS2_CLIENT_SHELL.md §4). The extended Q/W/E/R slots (DIK 0x10..0x13) exist
// in the original but are not drawn here.
inline constexpr int kQuickSlotCount = 10;

// Content type bound to a quickslot (UI_QuickSlot_AssignHotkey 0x5bdf00).
enum class QuickSlotType : uint8_t {
    Empty = 0,
    Item  = 1,   // consumable/autoplay object
    Skill = 2,   // skill
};

// A quickslot: what is assigned to it (no rendering data here).
struct QuickSlot {
    QuickSlotType type  = QuickSlotType::Empty;
    uint32_t      refId = 0;   // linked itemId or skillId
    bool empty() const { return type == QuickSlotType::Empty; }
};

// Simple screen rectangle (the original rects are int quadruplets).
struct HudRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// -----------------------------------------------------------------------------
// GameHud — always-visible HUD (vitals bars + quickslots + portrait frame).
// Owns its own SpriteBatch and Font (no shared instance exists in App yet). To be
// rendered every frame when scene == InGame.
class GameHud {
public:
    // Constructor AND destructor declared here, DEFINED in GameHud.cpp (not
    // `= default` inline): quickBarWindow_ below is a
    // std::unique_ptr<ConsumableBarWindow> over a type ONLY forward-declared in
    // this header (cf. banner above, include cycle with UI/ConsumableBarWindow.h).
    // Even a trivial default constructor needs the complete type here (the
    // generated constructor's implicit exception cleanup must know how to destroy
    // quickBarWindow_ if a later member throws): leaving it `= default` in EVERY
    // translation unit that constructs a GameHud (e.g. Scene/SceneManager.cpp,
    // which doesn't include ConsumableBarWindow.h) gives the same C2338 error as
    // for the destructor. An inline destructor here would instantiate
    // ~unique_ptr<ConsumableBarWindow>() with an incomplete type in EVERY
    // translation unit that includes GameHud.h (C2338 "can't delete an incomplete
    // type"); only GameHud.cpp includes the complete type.
    GameHud();
    ~GameHud();
    GameHud(const GameHud&)            = delete;
    GameHud& operator=(const GameHud&) = delete;

    // cGameHud_InitLayout 0x62A5B0: creates the sprite/font + white texture, then
    // pre-fills the layout rects from the screen dimensions. Returns false if the
    // device is null or a GPU resource fails.
    bool Init(gfx::Renderer& renderer, int screenW, int screenH);

    // Releases sprite/font/texture (App_Shutdown / UI teardown).
    void Shutdown();

    // cGameHud_Render 0x64A900: draws frame + HP/MP bars (game::g_World.self)
    // + quickslot bar. No-op if hidden or uninitialized.
    void Render();

    // cGameHud_OnMouseDown 0x62B080: hit-tests the quickslots (and the frame).
    // Returns true if the event was consumed ("first consumer wins").
    bool OnMouseDown(int x, int y);

    // Around a D3D9 device Reset().
    void OnDeviceLost();
    void OnDeviceReset();

    // Visible state (this[175] / bVisible in the original).
    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const    { return visible_; }

    // Quickslot access (assigned by the hotkey system).
    QuickSlot&       Slot(int i)       { return slots_[static_cast<size_t>(i)]; }
    const QuickSlot& Slot(int i) const { return slots_[static_cast<size_t>(i)]; }

    // Last clicked slot (-1 if none) — hook point for the use action.
    int LastClickedSlot() const { return lastClickedSlot_; }

    // Minimap (§12 of Docs/TS2_UI_GAMEHUD_RENDER.md) — direct access for future
    // external wiring (e.g. shortcut key toggling size, quest system ->
    // SetQuestHighlightMonster). Wired in Init/Render/OnMouseDown below; see
    // UI/MinimapWidget.h.
    MinimapWidget&       Minimap()       { return minimap_; }
    const MinimapWidget& Minimap() const { return minimap_; }

    // Buff/debuff grid (§9) + bottom-right status panel (§16) — direct access for
    // future external wiring (e.g. a buff system pushing into
    // game::PlayerEntity::buffs, SetStatusFlag/SetCasting hooks). Wired in
    // Init/Render/OnMouseDown/OnDeviceLost/OnDeviceReset below; see
    // UI/BuffStatusPanel.h and Docs/TS2_UI_GAMEHUD_RENDER.md §9/§16.
    BuffStatusPanel&       Buffs()       { return buffPanel_; }
    const BuffStatusPanel& Buffs() const { return buffPanel_; }

    // Chat & system message window (§13) — wired mission 2026-07-14 (see banner
    // above). Direct access exposed for future external wiring:
    //   - Chat().Bind(netClient): requires SceneManager to own/expose a
    //     net::NetClient& to pass here (no instance reachable from GameHud today)
    //     — precise TODO, cf. .cpp banner.
    //   - Chat().OnKey(vk) / Chat().OnChar(c): requires
    //     SceneManager::OnKeyDown/OnChar to route to hud_ in the InGame scene
    //     (today these two methods route ONLY to `login_`, the Login scene) —
    //     precise TODO, cf. .cpp banner. Without this wiring the chat window
    //     DISPLAYS incoming messages but cannot receive keyboard input.
    ChatWindow&       Chat()       { return chatWindow_; }
    const ChatWindow& Chat() const { return chatWindow_; }

    // Alliance/party frames (§8, EA 0x67B891-0x67BD54, cf. Docs/TS2_UI_GAMEHUD_RENDER.md
    // §8) — wired mission 2026-07-14 (see GameHud.cpp banner). No external access
    // needed to date (data source = game::g_World.allianceRoster +
    // game::g_World.players, already global): no dedicated public method.

private:
    // Rects computed once by Init (recomputed if the dims change).
    struct Layout {
        HudRect frame;     // vitals frame (translucent background)
        HudRect portrait;  // small portrait frame
        HudRect hpBar;     // HP bar
        HudRect mpBar;     // MP bar
        HudRect quickBar;  // quickslot bar background
        std::array<HudRect, kQuickSlotCount> slots{}; // individual slots
        HudRect questMarker; // §17 quest marker callout (Quest_DrawTracker 0x510FC0)
    };

    void InitLayout();

    // Drawing primitives (call between sprite_.Begin()/End()).
    void DrawFilledRect(const HudRect& r, D3DCOLOR color);
    void DrawBorder(const HudRect& r, int thickness, D3DCOLOR color);
    void DrawBarFill(const HudRect& r, int cur, int max,
                     D3DCOLOR bg, D3DCOLOR fill);
    // Discrete-step variant — FALLBACK for the 4 vitals bars when the atlas .IMG is
    // missing (wave W9). The binary only ever knows 41 steps:
    // `Crt_ftol(cur*41.0/max)` (UI_GameHud_Render 0x67A44B-0x67A45D), never a
    // continuous ratio. Deliberately SEPARATE from DrawBarFill above, which stays
    // continuous for §7/§8 (target plates / alliance frames: 36 steps in the
    // binary, out of scope for W9).
    void DrawBarFillQuantized(const HudRect& r, int cur, int max, int steps,
                              D3DCOLOR bg, D3DCOLOR fill);

    // --- Vitals bars as atlas frames (wave W9, HUD-02) -------------------------
    // The binary never draws a flat rect for the bars: it blits frame
    // `base + ftol(cur*41.0/max)` (clamped to base+41) from the shared Sprite2D
    // array g_AssetMgr_UiAtlasSlots 0x8E8B50 (stride 148), at its native size.
    // Proven bases (UI_GameHud_Render 0x67A3C0): HP 95 @0x67A462, MP 137 @0x67A515,
    // EXP 179 @0x67A65A, Mastery 3543 @0x67A707.
    // DrawAtlasFrame: blits array element `frame` (file 001_%05d.IMG, index frame+1
    // — same convention as kVitalsFrameImgPath, cf. AssetMgr_InitAllSlots 0x4DEB50).
    // DrawAtlasBar: computes the frame, then delegates. Both return false if the
    // texture is missing -> the caller falls back to DrawBarFillQuantized.
    bool DrawAtlasFrame(int frame, int x, int y);
    bool DrawAtlasBar(int baseFrame, int cur, int max, int x, int y);

    // Rendering sub-passes.
    void DrawVitalsFrame();     // frame + portrait + fill of the 4 bars
    void DrawQuickSlotFrames(); // quickslot bar cells
    // Reads its own HP/MP/currency values from game::g_World — do NOT revert to a
    // parameterized signature (wave W9): the binary REWRITES the HP source between
    // the bar and the text (`if (dword_1687370 < 0) dword_1687370 = 0` @0x67A499),
    // so any snapshot taken before DrawVitalsFrame() would show the pre-clamp value.
    void DrawTextPass();
    // §15 row of menu buttons (UI_GameHud_Render 0x685177+) — wave W9, HUD-03.
    void DrawMenuButtons();
    // §4 talisman badge (UI_GameHud_Render 0x67A787-0x67A826) — wave W9, HUD-09 partial.
    // Called from DrawVitalsFrame() (same sprite pass); see GameHud.cpp for the real
    // block structure (the gap folder's was inverted) and the TODO on the
    // currency/durability blocks left out of scope.
    void DrawTalismanBadge();

    // §17 GM-only debug time overlay (EA 0x686942, inside UI_GameHud_Render,
    // binary condition `dword_1676108 > 0 && g_GmAuthLevel > 0` @0x6868e8-0x6868f8).
    // AUTONOMOUS font batch (own BeginBatch/EndBatch, like buffPanel_/chatWindow_
    // below): silent no-op (zero render cost) if either condition is false. See
    // GameHud.cpp for the 5 source globals.
    void DrawDebugTimeOverlay();

    // §17 quest marker callout — Quest_DrawTracker 0x510FC0, called by
    // UI_GameHud_Render right after the bottom-right panel (EA 0x6868AB, cf. Docs/
    // TS2_UI_GAMEHUD_RENDER.md §17). DISTINCT from UI/QuestTrackerWindow.h (the
    // permanent top-right panel, wired via UI/GameWindows.h): this callout shows
    // only while `questMarker_.active` is true (armed by
    // game::Quest_UpdateMarkerTimer on a new or completed objective, decaying
    // after ~30s or when the target warehouse closes, cf. Game/ComboPickupTick.h).
    // See GameHud.cpp for the wiring detail (state ticked locally — Scene/
    // SceneManager.cpp is NOT modified, cf. the member's banner).
    void DrawQuestMarkerPanel(); // sprite pass (dot + frame)
    void DrawQuestMarkerText();  // standalone font pass (like DrawDebugTimeOverlay)

    // --- Alliance/party frames (§8, mission 2026-07-14) -----------------------
    // A resolved row = a non-empty slot of game::g_World.allianceRoster.memberNames
    // (0..4, EA 0x67B891: `Crt_Strcmp(g_AllianceRosterNames, &String) != 0`) cross-
    // referenced BY NAME with game::g_World.players[] (same method as the §7
    // target plate: lookup by name in the entity array, NOT by roster index — the
    // two arrays are independent, cf. Game/GameState.h::PartyRoster for the same
    // caveat on the party). See GameHud.cpp for the detail of the limits (no
    // maxHp/maxMp modeled for a remote entity -> gauge grayed "no data" rather
    // than an invented ratio).
    struct AllianceFrameRow {
        std::string name;
        bool resolved   = false; // entity found by name in game::g_World.players
        int  hp = 0, hpMax = 0; bool hpMaxKnown = false;
        int  mp = 0, mpMax = 0; bool mpMaxKnown = false;
    };
    std::vector<AllianceFrameRow> BuildAllianceFrames() const;
    void DrawAllianceFramePanels(const std::vector<AllianceFrameRow>& rows);
    void DrawAllianceFrameText(const std::vector<AllianceFrameRow>& rows);
    // Coarse hit-test (rectangular zone covering the currently populated rows) —
    // same policy as layout_.frame/quickBar in OnMouseDown (click consumed, blocks
    // the click from reaching the 3D scene behind the HUD), no per-row sub-hit-test
    // (no associated action, pure info panel like UI/PartyWindow.cpp).
    bool AllianceFramesContains(int x, int y) const;

    IDirect3DDevice9*  device_ = nullptr;
    // NON-OWNING pointer to the gfx::Renderer passed to Init() — kept to populate
    // UiContext::renderer in Render() (cf. .cpp banner, audit 2026-07-14). AUDIT:
    // no current consumer of this local ctx (quickBarWindow_ ->
    // ConsumableBarWindow::Render) dereferences ctx.renderer today (it only uses
    // ctx.FillRect/DrawFrame/Text/MeasureText, which rely on
    // ctx.sprites/whiteTex/font) — so this is NOT the same silent bug as
    // LoginScene (no real rendering was suppressed). Populated anyway for
    // consistency with UIManager::Init (ctx_.renderer = renderer) and so as not to
    // trap a future ConsumableBarWindow extension that would load real item icons
    // via the PanelSkin pattern (EnchantWindow/WarehouseWindow/SkillTreeWindow/
    // VendorShopWindow already all use it via ctx.renderer).
    gfx::Renderer*     rendererPtr_ = nullptr;
    gfx::SpriteBatch   sprite_;
    gfx::Font          font_;
    IDirect3DTexture9* white_  = nullptr; // 1x1 white, tinted for flat rects

    // Real vitals-frame background sprite (Sprite2D_Draw &unk_8EC114 @0x67A43D,
    // identified as entry #93 of the shared Sprite2D array unk_8E8B50 ->
    // G03_GDATA/D01_GIMAGE2D/001/001_00094.IMG, cf. GameHud.cpp banner and
    // GameHud::Init). D3DPOOL_MANAGED (GpuTexture): survives device reset, no
    // special handling in OnDeviceLost/Reset.
    gfx::GpuTexture    vitalsFrameTex_;

    // Cache of the 4 vitals bars' atlas frames (wave W9, HUD-02) — key = path
    // "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG". Each bar can hit up to 42 distinct
    // frames depending on fill level (bases 95/137/179/3543, 41 steps): without a
    // cache, every frame would be re-decoded and re-uploaded EVERY frame. Load
    // failures are memoized by IconTextureCache (no retry per frame), which makes
    // the DrawBarFillQuantized fallback free when the .IMG files are missing.
    // D3DPOOL_MANAGED (GpuTexture): survives device reset, like vitalsFrameTex_.
    gfx::IconTextureCache vitalsAtlasCache_;

    int  screenW_ = 0;
    int  screenH_ = 0;
    bool visible_ = true;
    int  lastClickedSlot_ = -1;

    Layout layout_;
    std::array<QuickSlot, kQuickSlotCount> slots_{};

    // Minimap (§12) — standalone widget, draws through sprite_/font_/white_ above
    // (no GPU resource of its own). See UI/MinimapWidget.h/.cpp.
    MinimapWidget minimap_;

    // Buff grid (§9) + bottom-right status panel (§16) — AUTONOMOUS widget (its own
    // SpriteBatch/texture cache, cf. UI/BuffStatusPanel.h), unlike the minimap
    // above which reuses GameHud's sprite_/font_/white_. Needs its own
    // Init(renderer, &font_): BuffStatusPanel.cpp.
    BuffStatusPanel buffPanel_;

    // Chat window (§13) — lightweight widget (ChatWindow.h includes neither
    // <windows.h> nor <d3d9.h>, no cycle with this header), draws through its OWN
    // lazy internal ID3DXSprite (not sprite_/white_ above) but shares font_
    // (a Render parameter, not an owned resource). Wired mission 2026-07-14, see
    // banner above and in GameHud.cpp.
    ChatWindow chatWindow_;

    // §17 quest marker callout (Quest_DrawTracker 0x510FC0) — state OWNED by
    // GameHud, ticked every Render() via game::Quest_UpdateMarkerTimer (Game/
    // ComboPickupTick.h, already ported faithfully). Does NOT share the
    // `s_questMarker` instance local to the Scene/SceneManager.cpp lambda (that one
    // stays the "logic" source of truth — 600s spawn timer, notification sound —
    // but it is function-scope `static` in a file deliberately not modified by
    // this mission, hence invisible from here). Both instances converge to the
    // SAME state (active/markerVariant) because they read the same deterministic
    // inputs (game::g_QuestProgress + game::g_World.gameTimeSec, isArenaZone=false
    // here as there); the only accepted divergence: the "objective complete ->
    // nothing to do" branch consumes one extra Rng_Next() draw every 600s
    // (net::DefaultRng(), cf. Net/Rng.h banner — the server does not validate
    // these nonces, so no protocol impact; the only visible effect is a possibly
    // different callout-variant pick vs. the SceneManager copy, cosmetic only).
    game::QuestMarkerState questMarker_;

    // Real quickslot bar (§14, pixel counterpart of Game/ConsumableBarLogic.h) —
    // replaces the placeholder rendering of DrawQuickSlotFrames() above (kept as
    // fallback). Pointer because ConsumableBarWindow.h cannot be included here
    // (cycle, cf. forward declaration at the top of the file); allocated in
    // Init(), never null after a successful Init(). slots_ below stays the source
    // of truth it consumes (no duplicated data).
    std::unique_ptr<ConsumableBarWindow> quickBarWindow_;
};

} // namespace ts2::ui

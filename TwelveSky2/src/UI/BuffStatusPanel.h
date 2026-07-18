// UI/BuffStatusPanel.h — buff/debuff grid (7 columns) + bottom-right status
// panel (4 icons + cast indicator). Subset of UI_GameHud_Render
// 0x67A3C0, fully documented in Docs/TS2_UI_GAMEHUD_RENDER.md §9 (grid,
// EA 0x67BD54-0x67D9DA) and §16 (bottom-right panel, EA 0x6865BF-0x6868AB).
//
// ---------------------------------------------------------------------------------
// SCOPE AND SIMPLIFICATIONS (2026-07-14 mission):
//
//  §9 — the original grid has ~50 DISTINCT TRIGGER CONDITIONS, each reading a
//  global variable from a different game system (elemental combos, pair
//  synergy, skill loadout comparison, rank/grade, embedded weapon gem,
//  elemental harmony/mismatch, 36 timed debuffs, server time bonus, morph
//  food buff, elemental mastery, misc flags — cf. doc §9 points 1-14). This
//  class consumes a GENERIC model — the `game::PlayerEntity::buffs` vector of
//  `{id, expiryTime}` (see Game/GameState.h, struct ActiveBuff) — which works
//  for any of the ~50 original sources once an upstream system pushes state
//  into it, without ever blocking the widget's render while waiting.
//
//  PARTIAL WIRING (mission "CABLAGE GRILLE DE BUFFS" [buff grid wiring],
//  2026-07-14): of these ~50 conditions, 8 read an address that has a REAL
//  writer already wired on the Net/GameVarDispatch.cpp or
//  Net/CharStatDeltaDispatch.cpp side (i.e. a value that can actually become
//  non-zero in-game, not just a field that exists on paper): §9.5 (rank/grade),
//  §9.7 (2 of the 3 simple flags), §9.11 (server time bonus), §9.13 (elemental
//  mastery), §9.14 (3 of the 4 additional flags). `CollectWiredConditionBuffs`
//  (.cpp) reads them every frame via `game::g_Client.VarGet` and merges them
//  with `self.buffs` (still the anchor point for the ~40 other sources, not
//  wired here for lack of a confirmed writer OR due to a proven conflict with
//  another already-modeled system — exhaustive list and per-address
//  justification in CollectWiredConditionBuffs's header banner, .cpp). The
//  widget is therefore no longer empty by default: as soon as one of the 8
//  globals above takes a value (via an already-wired network handler), the
//  corresponding icon appears with no extra action.
//
//  §16 — the bottom-right frame and the 4 on/off icons are ACTUALLY resolved
//  .IMG files (see kStatusFrameFile / kStatusIcons below). The animated cast
//  indicator (8 frames, cycle `Crt_ftol(g_GameTimeSec*16)%8` @0x6865BF+) has NO
//  icon sequence identified in the disassembly — only the trigger formula
//  (`dword_1685E74[g_LocalElement]`) and the frame cycle are known — hence a
//  PERMANENT fallback to a pulsing pill + frame number text (this isn't a
//  load failure, the resource simply couldn't be identified this session).
//  TRIGGER WIRING (2026-07-14 mission, cf. UI/GameHud.cpp::Render() banner):
//  `SetCasting()` was written but never called -> pill permanently off. Now
//  driven every frame from `game::g_World.Self().anim.state`
//  (CharAnimState/ActionFsm, ALREADY modeled and kept up to date by
//  SceneManager::host.UpdateEntityAnimFrame): true when the state is
//  CastSlot0/1/2 (skill windup) or Channel, faithful to the original IDA
//  comment's semantics.
//
// ---------------------------------------------------------------------------------
// "REAL" ICON RESOLUTION METHOD (kKnownIcons / kStatusIcons):
//
// All icons not marked "pill" below are derived by the SAME static RE
// method already applied to unk_8EC114 in GameHud.cpp (see its header
// banner, steps 1-8): a symbolic address `unk_XXXXXX` cited by the
// decompiled UI_GameHud_Render is an element of the SHARED Sprite2D table
// for category 1 (base `unk_8E8B50`, stride 148 bytes, filled by
// `AssetMgr_InitAllSlots 0x4DEB50`: `for(i=0;i<4500;++i) Sprite2D_BuildPath(this+148*i+32, 1, i, 0)`,
// template `"G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"`, a3+1 = i+1). So for an
// address A belonging to this table: `i = (A - 0x8E8B50) / 148` (MUST divide
// exactly, remainder 0) and the corresponding file is `001_%05d.IMG` with
// `%05d = i + 1`. Verified: EXACT DIVISION (remainder 0) for the 42 addresses
// used in this file (33 in kKnownIcons + 9 in kStatusIcons/kStatusFrameFile),
// which statistically confirms their membership in this table rather than a
// numeric coincidence.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/GpuTexture.h"
#include "Game/GameState.h" // game::ActiveBuff (CollectWiredConditionBuffs, mission cablage 2026-07-14)

namespace ts2::ui {

// "Catalog" buff/debuff identifiers for the generic model
// `game::PlayerEntity::buffs` (Game/GameState.h, struct ActiveBuff::id). Values
// 0..kBuffKnownIconCount-1 point to an actually resolved .IMG icon (table
// kKnownIcons, §9 points 1-9 and 11.max/14); any OTHER value (negative id, or
// id >= kBuffKnownIconCount) falls back to a generic colored pill derived from
// the id (see PillColorForId, .cpp).
//
// UPDATE (wave W9, 2026-07-16): the bank of 36 timed debuffs §9.10 does NOT use
// this id space — it has its own atlas table `unk_A60D04`, now RESOLVED (it
// wasn't in previous passes): it's the base of CATEGORY 6 of
// AssetMgr_InitAllSlots 0x4DEB50 (@0x4DECEA:
// `Sprite2D_BuildPath(this + 5180*ii + 148*jj + 1540564, 6, ii, jj)`, with
// this = 0x8E8B30 -> 0x8E8B30 + 1540564 = 0xA60D04, EXACT), i.e. the template
// "G03_GDATA\D01_GIMAGE2D\007\007_%03d%03d.IMG" (Sprite2D_BuildPath 0x4D68E0 case 6,
// arguments a3+1 and a4+1). See GetBankIconTex (.cpp) and the GridEntry type below.
enum BuffIconId : int {
    kBuffComboA = 0, kBuffComboB, kBuffComboC,                    // §9.1 elemental combos (dword_184C218)
    kBuffElemState1, kBuffElemState2, kBuffElemState3, kBuffElemState4, // §9.2 local elemental state
    kBuffElemPair,                                                 // §9.3 elemental pair synergy
    kBuffLoadoutGood, kBuffLoadoutBad,                             // §9.4 skill loadout comparison
    kBuffRankDefault, kBuffRank1, kBuffRank2, kBuffRank3,          // §9.5 rank/grade bonus
    kBuffGem1, kBuffGem2, kBuffGem3, kBuffGem4,                    // §9.6 embedded weapon gem
    kBuffFlagA, kBuffFlagB, kBuffFlagC,                            // §9.7 simple status flags
    kBuffHarmony, kBuffMismatch,                                   // §9.8 elemental harmony/mismatch
    kBuffMisc1, kBuffMisc2, kBuffMisc3, kBuffMisc4, kBuffMisc5,    // §9.9 misc per-element bonus
    kBuffServerBonusMax,                                           // §9.11 server time bonus (==360 case)
    kBuffFlagAdd1, kBuffFlagAdd2, kBuffFlagAdd3, kBuffFlagAdd4, kBuffFlagAdd5, // §9.14 additional flags

    // --- Additions from mission "CABLAGE GRILLE DE BUFFS" [buff grid wiring]
    // (2026-07-14): dynamic sources (the global's VALUE, not just its sign,
    // selects the icon) -- cf. the .cpp's header banner (CollectWiredConditionBuffs)
    // for the verification method that confirmed these two sources as actually
    // modeled on the ClientRuntime side.
    kBuffElemMastery1, kBuffElemMastery2, kBuffElemMastery3, kBuffElemMastery4, // §9.13
    kBuffElemMastery5, kBuffElemMastery6, kBuffElemMastery7,                   // g_ElementMastery 1..7
    // confirmed by IDA (data_refs 0x1675680): original IDB comment "mastered
    // element 1..7 -> +1000 corresponding stat" -- a real UPPER bound, not a
    // guess.
    kBuffServerBonusMin2, kBuffServerBonusMin3, kBuffServerBonusMin4, kBuffServerBonusMin5, // §9.11
    // dword_1674AB0 in whole minutes (2..5); 360s (=6 min) remains covered by
    // kBuffServerBonusMax above (a different FIXED icon in the binary, not the
    // same per-minute indexed table).
    kBuffKnownIconCount
};

// ---------------------------------------------------------------------------------
// BuffStatusPanel — draws §9 (7-column grid) and §16 (bottom-right panel).
// STANDALONE class (like InventoryWindow): owns its own SpriteBatch and a
// lazy per-icon GpuTexture cache; receives the device via Init() and a
// SHARED font (not owned, like InventoryWindow::font_) for text.
// Does NOT implement the Dialog/UIManager interface: wired directly in
// GameHud.cpp (file-local instance), as documented in its header banner.
class BuffStatusPanel {
public:
    BuffStatusPanel() = default;
    ~BuffStatusPanel() { Shutdown(); }
    BuffStatusPanel(const BuffStatusPanel&)            = delete;
    BuffStatusPanel& operator=(const BuffStatusPanel&) = delete;

    // Takes the renderer's device + a shared font (not owned, can be
    // nullptr — falls back to icons/pills with no text).
    bool Init(gfx::Renderer& renderer, gfx::Font* font);
    void Shutdown();

    void SetScreenSize(int width, int height);

    // Draws §9 (buff grid) then §16 (bottom-right panel + cast). Manages its
    // own sprite_.Begin()/End() and its own font pass (shared font_, dedicated
    // batch): can be called independently of any other UI pass.
    void Render();

    // Minimal hit-test (grid + bottom-right panel): returns true if the click
    // falls in a zone managed by this widget (first-consumer-wins rule, cf.
    // doc §9 "All grid icons are clickable" — Sprite2D_HitTest ->
    // sub_4C1110(0), opens a generic tooltip/detail window). No action is
    // triggered here (no tooltip system modeled): only event consumption is
    // reproduced.
    bool OnMouseDown(int x, int y);

    // Around a D3D9 device Reset().
    void OnDeviceLost();
    void OnDeviceReset();

    // --- Bottom-right panel (§16): TODO hooks --------------------------------------
    // `this+176/+180/+184/+188` in the disassembly: 4 state booleans whose
    // semantics (which game systems arm them) could not be identified this
    // session (no named xref). Exposed for writing so a future game system
    // can drive them without touching this file again; false by default
    // ("normal/off" icon, never blocking).
    void SetStatusFlag(int index, bool active); // index 0..3
    // dword_1685E74[g_LocalElement]: elemental skill currently being windup/
    // channeled (animated cast icon overlaid on the 4th position, cf. doc §16).
    void SetCasting(bool casting) { casting_ = casting; }

private:
    struct TextItem { int x, y; std::string text; D3DCOLOR color; };

    // A slot actually drawn in the §9 grid. Two icon families COEXIST in the
    // binary, with DIFFERENT atlas tables:
    //   - conditions §9.1-9.9/9.11-9.14: FIXED icon from the cat.1 table (base
    //     unk_8E8B50), selected by a catalog id (BuffIconId);
    //   - bank of 36 timed debuffs §9.10: icon from the cat.6 table (base
    //     unk_A60D04), indexed by (local element, BANK INDEX) -- cf.
    //     GetBankIconTex (.cpp). This is the ONLY family that blinks/expires.
    // `game::ActiveBuff` (Game/GameState.h) cannot carry the bank index (no
    // field, and this header isn't modifiable from this front-end), hence this
    // local type that unifies both families under a single position cursor.
    struct GridEntry {
        int   catalogId = -1;    // BuffIconId (cat.1 icon); -1 if it's a bank entry
        int   bankIndex = -1;    // index 0..35 in the bank (cat.6 icon); -1 otherwise
        float remaining = -1.0f; // seconds remaining (bank only; -1 otherwise)
    };

    void RenderGrid();         // §9  EA 0x67BD54-0x67D9DA
    void RenderStatusPanel();  // §16 EA 0x6865BF-0x6868AB

    // Rebuilds EVERY FRAME (like the original binary, which never stores these
    // icons -- it recomputes them on every Render) the effective list to draw:
    // `self.buffs` (generic model for future network/expiry sources) + the §9
    // conditions whose data source is ACTUALLY modeled on the
    // game::ClientRuntime side (CollectWiredConditionBuffs) + the §9.10 bank
    // (CollectZoneStateBuffs). The INDEX in the returned vector IS the grid
    // position (the binary's var_424): appearing in it = occupying a slot, even
    // if the icon is hidden by blinking (cf. RenderGrid). Called by both
    // RenderGrid and OnMouseDown to stay consistent on the number of
    // displayed/clickable icons.
    std::vector<GridEntry> BuildGridEntries() const;

    // §9.10 -- bank of 36 timed debuffs (EA 0x67D560-0x67D77F). Pushes one entry
    // per slot that is PRESENT and outside the reserved [19,28] range. See the
    // implementation's (.cpp) banner for the layout (id/duration/timestamp) and guards.
    void CollectZoneStateBuffs(std::vector<GridEntry>& out) const;
    // Reads the `game::g_Client.VarGet(...)` globals already populated by
    // existing network handlers (Net/GameVarDispatch.cpp,
    // Net/CharStatDeltaDispatch.cpp) and pushes into `out` one ActiveBuff per §9
    // condition confirmed wirable this mission. See the implementation's (.cpp)
    // header banner for the exhaustive wired vs. discarded list (with
    // per-address justification).
    void CollectWiredConditionBuffs(std::vector<game::ActiveBuff>& out) const;

    // Solid tinted rect (scaled 1x1 white texture): "colored pill" fallback
    // when an .IMG icon isn't resolved (same principle as
    // WarehouseWindow::Render, which falls back to a colored `ctx.FillRect`
    // for its cells — here without a UiContext, hence the local primitive).
    void DrawFilledRect(int x, int y, int w, int h, D3DCOLOR color);
    void DrawBorder(int x, int y, int w, int h, int thickness, D3DCOLOR color);

    // Grid icon (catalog id -> texture, cached by id); nullptr if outside the
    // known table or if loading fails (=> pill in DrawGridIcon).
    gfx::GpuTexture* GetGridIconTex(int buffId);
    // Bottom-right panel / frame icon (direct fileNo -> texture, separate
    // cache so it doesn't collide with the grid ids, which share the range
    // [0, kBuffKnownIconCount)).
    gfx::GpuTexture* GetPanelIconTex(int fileNo);
    // Icon for a §9.10 bank slot: cat.6 table indexed by
    // (local element, bank index) -- RESOLVED to an .IMG file, see .cpp.
    // Dedicated cache (bankIconCache_): the key is a pair, it can't share the
    // id space of gridIconCache_/panelIconCache_.
    gfx::GpuTexture* GetBankIconTex(int element, int bankIndex);

    // Draws a grid slot: real icon if resolved, otherwise a colored pill
    // derived from the id (PillColorForId, .cpp).
    void DrawGridIcon(int buffId, int x, int y, int size);
    // Draws a slot of the unified model (catalog OR bank), with the same
    // "pill" fallback as DrawGridIcon when the icon isn't resolved.
    void DrawEntryIcon(const GridEntry& e, int x, int y, int size);

    IDirect3DDevice9*  device_ = nullptr;
    gfx::Font*          font_  = nullptr; // shared, not owned
    gfx::SpriteBatch     sprite_;
    IDirect3DTexture9*    white_ = nullptr; // tintable 1x1 white (pills/frames)
    gfx::GpuTexture        statusFrameTex_; // §16 frame (real file, D3DPOOL_MANAGED)

    std::unordered_map<int, gfx::GpuTexture> gridIconCache_;  // key = BuffIconId
    std::unordered_map<int, gfx::GpuTexture> panelIconCache_; // key = .IMG file number
    std::unordered_map<int, gfx::GpuTexture> bankIconCache_;  // key = element*100 + bankIndex (cf. GetBankIconTex)

    std::vector<TextItem> pendingText_; // deferred text pass (outside the sprite batch)

    int  screenW_ = 0;
    int  screenH_ = 0;

    bool statusFlags_[4] = { false, false, false, false }; // this+176..+188
    bool casting_        = false;                          // dword_1685E74[elem]

    // --- Grid geometry (§9, EA 0x67BD54: position `(220+28*(j%7), 28*(j/7)+5)`) ---
    static constexpr int kGridX      = 220;
    static constexpr int kGridY      = 5;
    static constexpr int kGridCols   = 7;
    static constexpr int kIconPitch  = 28;
    static constexpr int kIconSize   = 24;
    // Defensive cap (the original has no hard ceiling, but a safeguard avoids
    // an oversized buff vector saturating the screen): 12 rows = 84 icons.
    static constexpr int kGridMaxIcons = kGridCols * 12;

    // --- Bottom-right panel geometry (§16, frame `unk_94041C` anchored at
    // `(nWidth-width, nHeight-height)`) ---
    static constexpr int kStatusFrameFile   = 2424; // unk_94041C
    static constexpr int kStatusFallbackW   = 150;
    static constexpr int kStatusFallbackH   = 40;
    static constexpr int kStatusIconSize    = 24;
    struct StatusIconOffset { int dx, dy; };
    static constexpr StatusIconOffset kStatusOffsets[4] = {
        { 2, 6 }, { 59, 6 }, { 87, 6 }, { 115, 6 },
    };
};

} // namespace ts2::ui

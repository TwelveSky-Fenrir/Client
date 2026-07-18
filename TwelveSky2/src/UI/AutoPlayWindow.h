// UI/AutoPlayWindow.h — "AutoPlay" panel (automated farming) (ts2::ui).
//
// UI counterpart of Game/AutoPlaySystem.h, EXPLICITLY deferred by that logic
// module (cf. AutoPlaySystem.h §SCOPE: "NOT the rendering of autoplay UI
// panels"). This window only DISPLAYS/DRIVES a ts2::game::AutoPlaySystem
// already constructed elsewhere (one instance per local player, cf.
// AutoPlaySystem.h — no imposed singleton): it reimplements NO targeting
// logic, it reads `Targets()`/`CurrentTargetIndex()` and calls
// `ResetTargetList()`.
//
// SETTINGS TAB (showSettings_): faithful counterpart of the original binary's
// AutoPlay settings panel, disassembled in addition to the mission (cluster
// AutoPlay_DrawSettingsPanel 0x4593C0 / AutoPlay_OnClickSettings 0x45A0D0, DISTINCT
// from the AutoPlay_* 0x457EA0..0x45D080 cluster covered by Game/AutoPlaySystem.h).
// Exposes the 8 REAL fields of AutoPlayConfig with their TRUE ranges, confirmed by
// reading the hit-tests/writes of these two functions (global addresses g_AutoHuntMode
// 0x16755F4, g_AutoHuntSkillSingle 0x16755F8, g_AutoHuntSkillAoE 0x1675600,
// g_AutoHuntAoEThreshold 0x1675608, g_AutoHuntPkFactionMask 0x167560C,
// g_AutoHuntBagFullReturn 0x1675610, g_AutoHuntUseReturnScroll 0x1675618,
// g_AutoHuntUseTownItem 0x167561C):
//   - mode              : 2-state toggle (0/1) — the original panel only lets you
//                          click 0 or 1 (AutoPlay_OnClickSettings forces
//                          g_AutoHuntMode=1 or =0 depending on the clicked sprite),
//                          although a 3rd value (2) exists elsewhere in the binary
//                          for this field (not settable from this panel) — cf.
//                          AutoPlaySystem.h.
//   - aoeThreshold      : -/+ buttons clamped to [1,5] (AutoPlay_OnClickSettings's
//                          `for i=1;i<6` loop — NOT [1,15] like the target list size).
//   - pkFactionMask     : 4 independent checkboxes (bits 1/2/4/8 = factions 1..4,
//                          XOR on click in AutoPlay_OnClickSettings).
//   - warpOnStuck       : "return to town if bag full" checkbox (real IDB name
//                          g_AutoHuntBagFullReturn, cf. AutoPlaySystem.h comment).
//   - useReturnScroll / useTownItem : checkboxes, trivial [0,1] bounds.
//   - skillSingle / skillAoE : in the original, assigned by drag-and-drop of a skill
//                          icon (AutoPlay_DrawItemSlots 0x459140, out of pixel-exact
//                          scope here); this port exposes a numeric field clamped to
//                          [0,350] (0 = not configured), the REAL skillId range
//                          confirmed by Game/SkillSystem.h (SkillLevelTable, skillId 1..350).
// The Start/Stop toggle emits Net_SendOp99 (opcode 0x63, 125 bytes): PROVEN by
// AutoPlay_OnMouseUpMain 0x45A980 (START branch unk_9647F8 -> Net_SendOp99(1) @0x45AAD1;
// STOP branch unk_964920 -> Net_SendOp99(0) @0x45AB88). The old "TODO(send): no
// dedicated toggle opcode" note was WRONG: Op99's a2 argument IS the switch. Wired in
// OnClick (on release) via net::GlobalNetClient() + the EXISTING builder
// net::Net_SendAutoHuntSync (alias of Net_SendOp99, Net/SendPackets.h:269).
//
// Local state owned by THIS window:
//   - enabled_ : C++ mirror of g_InvDirtyEnable 0x16755AC — the binary's MASTER 0/1
//     auto-hunt flag (gates AutoPlay_Update, AND is the a2 argument serialized by Op99;
//     the old note "AutoPlaySystem has no active flag" was wrong). Default
//     false = stopped at startup, faithful: g_InvDirtyEnable lives in BSS (0 at boot),
//     read by the START guard `if (!g_InvDirtyEnable)` 0x45AA7D. ToggleAutoHunt() copies
//     enabled_ into AutoPlaySystem::externalState.invDirtyEnable (write-through) for the
//     gameplay side. The caller (game loop) reads IsEnabled() to gate
//     AutoPlaySystem::Update(dt).
//     NB centralization: the binary has only ONE g_InvDirtyEnable 0x16755AC; the rewrite
//     has several unmerged mirrors (externalState.invDirtyEnable defaults to `true` in
//     Game/AutoPlaySystem.h — not owned — and g_Client.Var(WarpAddr::InvDirtyEnable) on the
//     Game/MapWarp.cpp side). enabled_ (default false) remains the faithful
//     display/decision source at boot; merging the mirrors is out of scope for these
//     owned files.
//   - showSettings_ : Targets/Settings toggle, simplified counterpart of the original
//     panel's 3 tabs Start/Stop/Settings (unk_9647F8/964920/964A48 at
//     AutoPlay_OnClickSettings+0x2C..+0xD1) — Start/Stop are already covered by the
//     existing "AutoPlay active" checkbox, only the Settings tab was missing.
//
// No monster name data is exposed by Game/GameState.h for MonsterEntity
// (MONSTER_INFO has no typed accessor in Game/GameDatabase.h, unlike
// ITEM_INFO::name): each slot therefore shows the world index
// (g_World.monsters), the distance, and raw HP (MonsterEntity::hp), NOT a
// made-up name.
#pragma once
#include "UI/UIManager.h"
#include "Game/AutoPlaySystem.h"
#include "Game/GameState.h"

#include <cstdint>
#include <string>

namespace ts2::ui {

// AutoPlayWindow — modal dialog, mandatory close button (X in the
// top-right corner). Recenters every frame (cf. Dialog::x_/y_) like the other
// modal dialogs of the original client.
class AutoPlayWindow : public Dialog {
public:
    AutoPlayWindow() = default;
    // Convenience constructor: wires the local player's farming system
    // directly (reference NOT owned — lifetime managed by the caller).
    explicit AutoPlayWindow(game::AutoPlaySystem& system) : system_(&system) {}

    // Wires/unwires the driven system (allows a default-constructed
    // AutoPlayWindow() before the player's AutoPlaySystem exists).
    void SetSystem(game::AutoPlaySystem* system) { system_ = system; }
    game::AutoPlaySystem* System() const { return system_; }

    // Local "AutoPlay active" state — see banner above. The caller (game
    // loop) reads IsEnabled() to decide whether to call AutoPlaySystem::Update(dt).
    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override; // Escape closes the panel

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x = 0, y = 0, w = 0, h = 0;
        bool Contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    // One row of the 15-slot list, derived from AutoPlayTargetSlot +
    // MonsterEntity (g_World.monsters[monsterIndex]) at Render time.
    struct RowView {
        bool     used = false;       // slot occupied (monsterIndex >= 0)
        int32_t  monsterIndex = -1;
        float    distance = 0.0f;
        bool     available = false;  // AutoPlayTargetSlot::available (not taken by another)
        bool     locked = false;     // == AutoPlaySystem::CurrentTargetIndex()
        int      hp = 0;
        bool     hasHp = false;      // false if monsterIndex is out of g_World.monsters bounds
    };

    // Recomputes all the geometry (panel, checkbox, clear button, close
    // X, 15 rows) from the current screen dimensions. Called in
    // BOTH Render phases (identical result within the same frame, like
    // QuestTrackerWindow::BuildLayout) then cached for the deferred hit-test
    // (OnMouseDown/OnClick routed across two frames).
    void RecomputeLayout(int screenW, int screenH);
    RowView BuildRow(int slotIndex) const;

    // Start/Stop toggle for auto-hunt: reproduces AutoPlay_OnMouseUpMain 0x45A980
    // (START guards + Net_SendOp99 opcode 0x63 emission, or unconditional STOP). Called
    // from OnClick (on release), never from OnMouseDown (no optimistic effect).
    void ToggleAutoHunt();

    game::AutoPlaySystem* system_ = nullptr;

    bool enabled_ = false; // "AutoPlay active" checkbox (local state, cf. banner)

    // Button latches (armed on mouse-down, resolved on release —
    // same pattern as MsgBoxDialog::btnPressed_).
    bool closeArmed_ = false;
    bool clearArmed_ = false;
    bool checkArmed_ = false;

    // Geometry cached from the last Render (recentered every frame).
    Rect panel_;
    Rect closeBtn_;
    Rect checkbox_;
    Rect checkboxLabel_; // extended clickable area (checkbox + label)
    Rect clearBtn_;
    Rect rows_[15];

    static constexpr int kPanelW    = 240;
    static constexpr int kPadX      = 10;
    static constexpr int kPadY      = 10;
    static constexpr int kTitleH    = 20;
    static constexpr int kCheckH    = 18;
    static constexpr int kRowH      = 14;
    static constexpr int kRowCount  = 15;
    static constexpr int kButtonH   = 22;
    static constexpr int kCloseSize = 16;
    static constexpr int kCheckSize = 12;

    static constexpr D3DCOLOR kColBg       = Argb(224, 32, 32, 40);    // ~0xE0202028
    static constexpr D3DCOLOR kColBorder   = Argb(255, 128, 128, 128); // ~0xFF808080
    static constexpr D3DCOLOR kColTitle    = Argb(255, 255, 221, 102); // ~0xFFFFDD66
    static constexpr D3DCOLOR kColText     = Argb(255, 255, 255, 255); // ~0xFFFFFFFF
    static constexpr D3DCOLOR kColHover    = Argb(255, 64, 96, 160);   // ~0xFF4060A0
    static constexpr D3DCOLOR kColError    = Argb(255, 255, 96, 96);   // ~0xFFFF6060
    static constexpr D3DCOLOR kColSuccess  = Argb(255, 96, 255, 96);   // ~0xFF60FF60
    static constexpr D3DCOLOR kColDim      = Argb(255, 140, 140, 140); // free slot / not targetable
    static constexpr D3DCOLOR kColButtonBg = Argb(255, 60, 60, 72);
};

} // namespace ts2::ui

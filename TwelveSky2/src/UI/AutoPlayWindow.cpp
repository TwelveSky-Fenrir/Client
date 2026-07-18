// UI/AutoPlayWindow.cpp — implementation of the "AutoPlay" panel.
// See UI/AutoPlayWindow.h for the contract and caveats on displayed data
// (no monster name available). The Start/Stop toggle emits
// Net_SendOp99 (opcode 0x63) — the "active" state (enabled_) is the serialized mirror
// of g_InvDirtyEnable 0x16755AC, NOT a purely local flag.
#include "UI/AutoPlayWindow.h"
#include "UI/PanelSkin.h"
// Auto-hunt sync emission (Net_SendOp99 0x4BD140, opcode 0x63, 125 bytes): EXISTING builder
// net::Net_SendAutoHuntSync (Net/SendPackets.h:269) + g_NetClient singleton
// 0x8156A0 restored via net::GlobalNetClient() (Net/NetClient.h:67-68). System messages via
// game::g_Client.msg (Msg_AppendSystemLine 0x68D9D0) + game::Str (StrTable005_Get 0x4C1D10).
// NB: Net/SendPackets.h already includes NetClient.h (same winsock order as CharacterStatsWindow.cpp).
#include "Net/SendPackets.h"
#include "Game/ClientRuntime.h"

#include <cstdio>
#include <cstring>

namespace ts2::ui {

namespace {
// Real panel background (best effort): (400,440) template from the UI atlas folder
// G03_GDATA/D01_GIMAGE2D/001 — candidate NOT CONFIRMED by IDA, chosen by
// aspect-ratio proximity to the AutoPlay panel (240 x ~304 per
// RecomputeLayout, closest ratio among identified templates; see
// detailed methodology in UI/PanelSkin.h). Distinct index from the ones
// used by OptionsWindow/VendorShopWindow (same cluster). Automatic
// fallback to kColBg if missing.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_03641.IMG");

// Serialization of the 44-byte auto-hunt config blob emitted by Op99 (packet offset +81; original
// contiguous region g_AutoHuntMode 0x16755F4..0x1675620). FIELD BY FIELD at wire offsets
// PROVEN (RE Net_SendOp99 0x4BD140): AutoPlayConfig is NOT wire-compatible (different
// order — skillAoE at +8 in struct vs +12 on wire; bool instead of int32; 3 wire fields
// missing) -> memcpy(&config, 44) FORBIDDEN. Each field = int32 LE (native x86).
// Offset shown = position WITHIN the blob (corresponding packet offset = +81 + offset).
void BuildAutoHunt44(const game::AutoPlayConfig& c, uint8_t out[44]) {
    int32_t w[11] = {0};
    w[0]  = c.mode;                                // +0  (pkt+81)  g_AutoHuntMode            0x16755F4
    w[1]  = static_cast<int32_t>(c.skillSingle);   // +4  (pkt+85)  g_AutoHuntSkillSingle     0x16755F8
    w[2]  = 0; // +8  (pkt+89)  g_AutoHuntSkillSingleOn 0x16755FC — absent from AutoPlayConfig
    w[3]  = static_cast<int32_t>(c.skillAoE);      // +12 (pkt+93)  g_AutoHuntSkillAoE        0x1675600
    w[4]  = 0; // +16 (pkt+97)  g_AutoHuntSkillAoEOn   0x1675604 — absent from AutoPlayConfig
    w[5]  = c.aoeThreshold;                        // +20 (pkt+101) g_AutoHuntAoEThreshold    0x1675608
    w[6]  = static_cast<int32_t>(c.pkFactionMask); // +24 (pkt+105) g_AutoHuntPkFactionMask   0x167560C
    w[7]  = c.warpOnStuck ? 1 : 0;                 // +28 (pkt+109) g_AutoHuntBagFullReturn   0x1675610
    w[8]  = 0; // +32 (pkt+113) g_AutoHuntSettingsDirty 0x1675614 — absent from AutoPlayConfig
    w[9]  = c.useReturnScroll ? 1 : 0;             // +36 (pkt+117) g_AutoHuntUseReturnScroll 0x1675618
    w[10] = c.useTownItem ? 1 : 0;                 // +40 (pkt+121) g_AutoHuntUseTownItem     0x167561C
    // TODO [0x16755FC / 0x1675604 / 0x1675614]: skillSingleOn / skillAoEOn / settingsDirty
    //   not modeled in AutoPlayConfig (Game/AutoPlaySystem.h, not owned by this wave)
    //   -> emitted as 0 (faithful "not configured" default, NOT a fabrication). Needs modeling on
    //   the AutoPlaySystem side for full wire fidelity.
    std::memcpy(out, w, 44);
}

// EmitAutoHuntSync — emits Net_SendOp99 (opcode 0x63, 125 bytes) via the global g_NetClient.
// The binary emits UNCONDITIONALLY from &g_AutoPlayMgr (global buffer, NOT an
// autoplay manager) without receiving the socket; on the C++ side we read the restored
// singleton net::GlobalNetClient() (0x8156A0).
void EmitAutoHuntSync(game::AutoPlaySystem& sys, net::NetClient* nc, int8_t stateFlag) {
    if (!nc) return; // out of session: no send. In scene 6 nc is non-null (NOT dead code).

    // 68-byte appearance/quick-skills blob: byte_16755B0 (u32) + g_AutoHuntQuickSkills 0x16755B4
    // (8×{id:u32, on:u32}). NO C++ model: written by UI_QuickSlot_AssignHotkey 0x5BDF00,
    // front-end NOT owned by this wave. Hosted in ClientRuntime's shared long-tail blob
    // (key = original address 0x16755B0) — zeros until the quickslot front-end
    // feeds it = faithful "no quick-skill assigned" default state (NOT a fabrication).
    // TODO [0x16755B4 / UI_QuickSlot_AssignHotkey 0x5BDF00]: wire up the real 68-byte content
    //   (also unblocks the TODO(net/state) in Game/MapWarp.cpp:179/205).
    const void* appearance68 = game::g_Client.Blob(0x16755B0, 68).data();

    uint8_t autoHunt44[44];
    BuildAutoHunt44(sys.config, autoHunt44);

    // Net_SendAutoHuntSync = Net_SendOp99 0x4BD140. stateFlag emitted as 4 bytes LE
    // (WriteChar4LE, proven thiscall gotcha 0x4BD1DF -> the server reads an int32).
    net::Net_SendAutoHuntSync(*nc, stateFlag, appearance68, autoHunt44);
}
} // namespace

// Geometry
void AutoPlayWindow::RecomputeLayout(int screenW, int screenH) {
    const int listH = kRowCount * kRowH;
    const int h = kPadY + kTitleH + kCheckH + 6 + listH + 8 + kButtonH + kPadY;
    const int w = kPanelW;
    const int px = (screenW - w) / 2;
    const int py = (screenH - h) / 2;

    panel_ = { px, py, w, h };
    // Dialog::x_/y_: recentered every frame, like the other modal dialogs.
    x_ = px;
    y_ = py;

    closeBtn_ = { px + w - kPadX - kCloseSize, py + (kTitleH - kCloseSize) / 2,
                  kCloseSize, kCloseSize };

    const int cy = py + kTitleH;
    checkbox_ = { px + kPadX, cy + (kCheckH - kCheckSize) / 2, kCheckSize, kCheckSize };
    checkboxLabel_ = { px + kPadX, cy, w - 2 * kPadX, kCheckH }; // checkbox + label, clickable

    const int listY = cy + kCheckH + 6;
    for (int i = 0; i < kRowCount; ++i)
        rows_[i] = { px + kPadX, listY + i * kRowH, w - 2 * kPadX, kRowH };

    const int btnY = listY + listH + 8;
    clearBtn_ = { px + kPadX, btnY, w - 2 * kPadX, kButtonH };
}

// Derives a slot's display state from AutoPlaySystem::Targets() +
// (if the referenced monster is still alive in g_World.monsters) its HP.
// NO name: MONSTER_INFO has no typed accessor in Game/GameDatabase.h.
AutoPlayWindow::RowView AutoPlayWindow::BuildRow(int slotIndex) const {
    RowView r;
    if (!system_) return r;

    const auto& targets = system_->Targets();
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= targets.size()) return r;

    const game::AutoPlayTargetSlot& slot = targets[static_cast<size_t>(slotIndex)];
    r.used         = slot.monsterIndex >= 0;
    r.monsterIndex = slot.monsterIndex;
    r.distance     = slot.distance;
    r.available    = slot.available;
    r.locked       = r.used && (slot.monsterIndex == system_->CurrentTargetIndex());

    if (r.used) {
        const auto& monsters = game::g_World.monsters;
        if (static_cast<size_t>(slot.monsterIndex) < monsters.size()) {
            r.hp    = monsters[static_cast<size_t>(slot.monsterIndex)].hp;
            r.hasHp = true;
        }
    }
    return r;
}

// Mouse / keyboard events
bool AutoPlayWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    if (!panel_.Contains(x, y)) return false;

    // Latches armed on mouse-DOWN only (the binary sets *this/*(this+1) on
    // mouse-down, cf. AutoPlay_OnMouseUpMain 0x45A9BE/0x45AB69); the ACTION (Start/Stop toggle
    // + Op99 emission) is DEFERRED to mouse-up (OnClick). NO optimistic effect here:
    // the old `enabled_ = !enabled_` on mouse-down was the "local effect the
    // binary doesn't do" bug (same family as the pass-3 EnchantWindow bug) -> removed.
    closeArmed_ = closeBtn_.Contains(x, y);
    clearArmed_ = clearBtn_.Contains(x, y);
    checkArmed_ = checkboxLabel_.Contains(x, y);

    return true; // click inside the panel: always consumed (first-consumer rule)
}

bool AutoPlayWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    const bool inPanel = panel_.Contains(x, y);

    if (closeArmed_) {
        closeArmed_ = false;
        if (closeBtn_.Contains(x, y)) {
            Close();
            return true;
        }
    }
    if (clearArmed_) {
        clearArmed_ = false;
        if (clearBtn_.Contains(x, y) && system_) {
            system_->ResetTargetList(); // AutoPlay_ResetTargetList 0x458AB0 — purely local
            return true;
        }
    }
    if (checkArmed_) {
        checkArmed_ = false;
        if (checkboxLabel_.Contains(x, y)) {
            // Start/Stop toggle emitted on RELEASE, faithful to AutoPlay_OnMouseUpMain
            // 0x45A980 (the binary toggles + emits on mouse-up, not mouse-down).
            ToggleAutoHunt();
            return true;
        }
    }
    return inPanel;
}

// Start/Stop toggle for auto-hunt — emits Net_SendOp99 (opcode 0x63).
// Faithful mirror of AutoPlay_OnMouseUpMain 0x45A980 (START branch unk_9647F8 /
// STOP branch unk_964920). This port's single checkbox plays both
// roles: START if auto-hunt is stopped (enabled_ == false), STOP otherwise.
void AutoPlayWindow::ToggleAutoHunt() {
    if (!system_) return; // the binary always has a state machine; without it, nothing to drive

    net::NetClient* nc = net::GlobalNetClient(); // &g_NetClient 0x8156A0 (non-null in scene 6)

    // enabled_ = mirror of g_InvDirtyEnable 0x16755AC (master 0/1 flag). The binary's START
    // guard is `if (!g_InvDirtyEnable)` 0x45AA7D: only (re)starts if stopped.
    if (!enabled_) {
        // ===== START ("Start" button unk_9647F8, 0x45A9BE) =====
        // Guard a (0x45AA01): dword_1673248 <= 0 || !AutoPlay_HasPotionsSet() -> refusal 1790.
        //   dword_1673248 = externalState.classItemId (equipped "class core").
        //   AutoPlay_HasPotionsSet 0x45E700: NOT modeled in AutoPlaySystem -> partial
        //   guard (only the classItemId half is reproduced).
        // NB: classItemId is a uint32_t on the C++ side; the binary compares dword_1673248 as
        // SIGNED (`<= 0`), hence the int32_t cast (reproduces the signed compare, also avoids
        // the "unsigned <= 0 always == 0" warning).
        if (static_cast<int32_t>(system_->externalState.classItemId) <= 0
            /* TODO [0x45E700]: && AutoPlay_HasPotionsSet() — potion state not modeled */) {
            game::g_Client.msg.System(game::Str(1790)); // Msg_AppendSystemLine 1790 (0x45A9E4)
            return;                                       // 0x45A9F9: NO emission
        }
        // Guard b (0x45AA38): !AutoPlay_HasRequiredItems() -> refusal 1792.
        if (!system_->HasRequiredItems()) {              // AutoPlay_HasRequiredItems 0x45CC10
            game::g_Client.msg.System(game::Str(1792)); // Msg_AppendSystemLine 1792 (0x45AA52)
            return;                                       // 0x45AA67: NO emission
        }
        // 0x45AA6C: dword_1675B20 = -1 (resets the in-flight auto-scroll slot). PRIVATE state
        // of AutoPlaySystem (pendingItemUseSlot_) with no exposed public setter.
        // TODO [0x1675B20]: reset not reproducible without an AutoPlaySystem API (not owned).

        // Guard c (0x45AA8C): !AutoPlay_IsNpcTargetable(g_SelfMorphNpcId) -> refusal 2418.
        //   AutoPlay_IsNpcTargetable 0x45FD90 / g_SelfMorphNpcId 0x1675A98: NOT modeled.
        // TODO [0x45FD90 / 0x1675A98]: IsNpcTargetable guard not reproduced (state not modeled)
        //   -> emits directly (the binary also emits when the morph target is valid).

        enabled_ = true;                               // g_InvDirtyEnable = 1 (0x45AAC0)
        system_->externalState.invDirtyEnable = true;  // write-through to the gameplay mirror
        EmitAutoHuntSync(*system_, nc, 1);             // Net_SendOp99(1) (0x45AAD1)
        // 0x45AADB..0x45AB1F: cQuickSlotWin_Close / this[20]=0 / unfocus editbox / this[71]=0
        //   = state of other windows (quickslots / editbox), outside this widget.
        // TODO [cQuickSlotWin_Close 0x65F5A0]: quickslot closing not reproduced here.
        game::g_Client.msg.System(game::Str(1907));    // Msg_AppendSystemLine 1907 (0x45AB0C)
    } else {
        // ===== STOP ("Stop" button unk_964920, 0x45AB69): UNCONDITIONAL emission =====
        enabled_ = false;                              // g_InvDirtyEnable = 0 (0x45AB72)
        system_->externalState.invDirtyEnable = false; // write-through
        EmitAutoHuntSync(*system_, nc, 0);             // Net_SendOp99(0) (0x45AB88)
        system_->ResetTargetList();                    // AutoPlay_ResetTargetList 0x458AB0 (0x45AB90)
        game::g_Client.msg.System(game::Str(1908));    // Msg_AppendSystemLine 1908 (0x45ABA5)
    }
}

bool AutoPlayWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// Rendering
void AutoPlayWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Recomputes geometry in BOTH phases (identical result within the same
    // frame): needed so the Text phase aligns with the rects just
    // filled in the Panels phase, and so the deferred hit-test (routed
    // across two frames) stays accurate right after a screen resize.
    RecomputeLayout(ctx.screenW, ctx.screenH);
    if (!bOpen_) return;

    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, panel_.x, panel_.y, panel_.w, panel_.h, kColBg);
        ctx.DrawFrame(panel_.x, panel_.y, panel_.w, panel_.h, kColBorder, 1);

        // Close button (X), highlighted on hover.
        const bool closeHover = closeBtn_.Contains(cursorX, cursorY);
        ctx.FillRect(closeBtn_.x, closeBtn_.y, closeBtn_.w, closeBtn_.h,
                     closeHover ? kColHover : kColButtonBg);
        ctx.DrawFrame(closeBtn_.x, closeBtn_.y, closeBtn_.w, closeBtn_.h, kColBorder, 1);

        // "AutoPlay active" checkbox.
        ctx.FillRect(checkbox_.x, checkbox_.y, checkbox_.w, checkbox_.h,
                     enabled_ ? kColSuccess : kColButtonBg);
        ctx.DrawFrame(checkbox_.x, checkbox_.y, checkbox_.w, checkbox_.h, kColBorder, 1);

        // "Clear list" button.
        const bool clearHover = clearBtn_.Contains(cursorX, cursorY);
        ctx.FillRect(clearBtn_.x, clearBtn_.y, clearBtn_.w, clearBtn_.h,
                     clearHover ? kColHover : kColButtonBg);
        ctx.DrawFrame(clearBtn_.x, clearBtn_.y, clearBtn_.w, clearBtn_.h, kColBorder, 1);
        return;
    }

    // --- Text phase ---
    const char* title = "AutoPlay";
    ctx.Text(title, panel_.x + (panel_.w - ctx.MeasureText(title)) / 2, panel_.y + 4, kColTitle);
    ctx.Text("X", closeBtn_.x + (closeBtn_.w - ctx.MeasureText("X")) / 2, closeBtn_.y + 2, kColText);

    ctx.Text("AutoPlay actif", checkbox_.x + kCheckSize + 6, checkbox_.y - 1,
             enabled_ ? kColSuccess : kColText);

    if (!system_) {
        ctx.Text("(systeme AutoPlay non branche)", panel_.x + kPadX, rows_[0].y, kColError);
    } else {
        char buf[96];
        for (int i = 0; i < kRowCount; ++i) {
            const RowView row = BuildRow(i);
            const Rect& r = rows_[i];
            D3DCOLOR color;

            if (!row.used) {
                std::snprintf(buf, sizeof(buf), "%2d. --- libre ---", i + 1);
                color = kColDim;
            } else if (row.locked) {
                if (row.hasHp)
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f  PV=%d [VERROU]",
                                  i + 1, row.monsterIndex, row.distance, row.hp);
                else
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f [VERROU]",
                                  i + 1, row.monsterIndex, row.distance);
                color = kColTitle;
            } else if (!row.available) {
                std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f [pris]",
                              i + 1, row.monsterIndex, row.distance);
                color = kColError;
            } else {
                if (row.hasHp)
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f  PV=%d",
                                  i + 1, row.monsterIndex, row.distance, row.hp);
                else
                    std::snprintf(buf, sizeof(buf), "%2d. Monstre #%d  d=%.1f",
                                  i + 1, row.monsterIndex, row.distance);
                color = kColText;
            }
            ctx.Text(buf, r.x, r.y, color);
        }
    }

    const char* clearLabel = "Vider la liste";
    ctx.Text(clearLabel, clearBtn_.x + (clearBtn_.w - ctx.MeasureText(clearLabel)) / 2,
             clearBtn_.y + (clearBtn_.h - 12) / 2, kColText);
}

} // namespace ts2::ui

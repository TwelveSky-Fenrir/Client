// UI/GameHud_Alliance.cpp — GameHud alliance/party frames sub-pass (split out of
// GameHud.cpp for size; same class, see GameHud.cpp for the file-family banner and
// EA history). Holds BuildAllianceFrames, DrawAllianceFramePanels,
// DrawAllianceFrameText and AllianceFramesContains (§8, EA 0x67B891-0x67BD54,
// mission 2026-07-14).
#include "UI/GameHud.h"
#include "UI/GameHud_Internal.h" // shared constants (kAllyFrameBg/Brd, kAllyIconOnline/Offline, kAllyNoData, kAllyNameCol, kAllianceIconSize, kAllianceBarH, kHpBg/kHpFill, kMpBg/kMpFill, kBarBorder, kTextColor, kTextDim)
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
constexpr int kAllianceMaxRows  = 5;   // g_AllianceRosterNames: slot 0 = leader, 1..4 = members
constexpr int kAllianceRowY0    = 155; // EA 0x67B891: anchor (0,155+50*i)
constexpr int kAllianceRowStep  = 50;
constexpr int kAllianceRowW     = 204; // remaining space left of the §9 buff grid (x=220)
constexpr int kAllianceNameDy = 7;
constexpr int kAllianceHpDy   = 25;
constexpr int kAllianceMpDy   = 36;
} // namespace

// =============================================================================
// Alliance/party frames (§8, EA 0x67B891-0x67BD54) — mission 2026-07-14.
// =============================================================================

// Cross-references game::g_World.allianceRoster (names, EA-verified: g_AllianceRosterNames)
// with game::g_World.players[] BY NAME (same method as the §7 target plate in the
// original binary — the two arrays share no direct join key, cf. comment on
// Game/GameState.h::PartyRoster for the same caveat on the party). Loop stops at the
// first empty slot (faithful to the EA condition `Crt_Strcmp(...) != 0` tested
// sequentially in the binary, NOT a simple "skip empties" filter).
std::vector<GameHud::AllianceFrameRow> GameHud::BuildAllianceFrames() const {
    std::vector<AllianceFrameRow> rows;
    const auto& alliance = game::g_World.allianceRoster;
    const auto& players  = game::g_World.players;

    for (int i = 0; i < kAllianceMaxRows; ++i) {
        const std::string& nm = alliance.memberNames[static_cast<size_t>(i)];
        if (nm.empty()) break; // EA 0x67B891: loop while slot i is non-empty

        AllianceFrameRow row;
        row.name = nm;

        for (size_t pi = 0; pi < players.size(); ++pi) {
            const game::PlayerEntity& p = players[pi];
            if (!p.active || p.name != nm) continue;
            row.resolved = true;
            if (pi == 0) {
                // Slot 0 of the entity array = local player (EntityManager convention):
                // most reliable source (StatEngine), both current PV/PM AND real maxima available.
                const game::SelfState& self = game::g_World.self;
                row.hp = self.hp; row.hpMax = self.maxHp; row.hpMaxKnown = true;
                row.mp = self.mp; row.mpMax = self.maxMp; row.mpMaxKnown = true;
            } else {
                // Other member: real CURRENT HP/MP (Pkt_CharStatDelta writes e->hp/e->mp for
                // ANY entity resolved by network identity, not only self, cf.
                // Game/EntityManager.cpp::OnCharStatDelta) — but NO maximum is modeled for a
                // remote entity (PlayerEntity has no maxHp/maxMp, cf. Game/GameState.h):
                // hpMaxKnown/mpMaxKnown stay false, the gauge is drawn grayed rather than with
                // an invented ratio.
                row.hp = p.hp;
                row.mp = p.mp;
            }
            break;
        }

        rows.push_back(std::move(row));
    }
    return rows;
}

// Pass 1 (flat sprites): row background + presence dot + HP/MP gauges.
void GameHud::DrawAllianceFramePanels(const std::vector<AllianceFrameRow>& rows) {
    for (size_t i = 0; i < rows.size(); ++i) {
        const AllianceFrameRow& r = rows[i];
        const int rowY = kAllianceRowY0 + kAllianceRowStep * static_cast<int>(i);

        const HudRect frame{ 0, rowY, kAllianceRowW, kAllianceRowStep - 4 };
        DrawFilledRect(frame, kAllyFrameBg);
        DrawBorder(frame, 1, kAllyFrameBrd);

        // Presence dot (fallback: no .IMG asset identified, cf. GameHud.cpp banner —
        // the binary distinguishes max-level/normal/not-found icons, reduced here to
        // present/absent, the only information actually available client-side).
        const HudRect icon{ 4, rowY + 4, kAllianceIconSize, kAllianceIconSize };
        DrawFilledRect(icon, r.resolved ? kAllyIconOnline : kAllyIconOffline);
        DrawBorder(icon, 1, kAllyFrameBrd);

        const HudRect hpBar{ 5, rowY + kAllianceHpDy, kAllianceRowW - 10, kAllianceBarH };
        const HudRect mpBar{ 5, rowY + kAllianceMpDy, kAllianceRowW - 10, kAllianceBarH };

        if (r.resolved && r.hpMaxKnown) {
            DrawBarFill(hpBar, r.hp, r.hpMax, kHpBg, kHpFill);
        } else {
            DrawFilledRect(hpBar, kAllyNoData);
            DrawBorder(hpBar, 1, kBarBorder);
        }
        if (r.resolved && r.mpMaxKnown) {
            DrawBarFill(mpBar, r.mp, r.mpMax, kMpBg, kMpFill);
        } else {
            DrawFilledRect(mpBar, kAllyNoData);
            DrawBorder(mpBar, 1, kBarBorder);
        }
    }
}

// Text pass: member name + real numeric values when available.
void GameHud::DrawAllianceFrameText(const std::vector<AllianceFrameRow>& rows) {
    if (!font_.Ready()) return;
    char buf[64];

    for (size_t i = 0; i < rows.size(); ++i) {
        const AllianceFrameRow& r = rows[i];
        const int rowY = kAllianceRowY0 + kAllianceRowStep * static_cast<int>(i);

        font_.DrawTextStyled(r.name.c_str(), kAllianceIconSize + 12, rowY + kAllianceNameDy,
                             r.resolved ? kAllyNameCol : kTextDim, gfx::kStyleShadow);

        if (r.resolved) {
            if (r.hpMaxKnown) {
                std::snprintf(buf, sizeof(buf), "%d/%d", r.hp, r.hpMax);
            } else {
                std::snprintf(buf, sizeof(buf), "%d", r.hp); // real HP, max not modeled
            }
            font_.DrawTextStyled(buf, 8, rowY + kAllianceHpDy - 1, kTextColor, gfx::kStyleShadow);

            if (r.mpMaxKnown) {
                std::snprintf(buf, sizeof(buf), "%d/%d", r.mp, r.mpMax);
            } else {
                std::snprintf(buf, sizeof(buf), "%d", r.mp);
            }
            font_.DrawTextStyled(buf, 8, rowY + kAllianceMpDy - 1, kTextColor, gfx::kStyleShadow);
        }
    }
}

// Coarse hit-test: zone covering the currently populated roster rows.
bool GameHud::AllianceFramesContains(int x, int y) const {
    int count = 0;
    const auto& alliance = game::g_World.allianceRoster;
    for (int i = 0; i < kAllianceMaxRows; ++i) {
        if (alliance.memberNames[static_cast<size_t>(i)].empty()) break;
        ++count;
    }
    if (count == 0) return false;
    const HudRect area{ 0, kAllianceRowY0, kAllianceRowW, kAllianceRowStep * count };
    return area.Contains(x, y);
}

} // namespace ts2::ui

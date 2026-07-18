// UI/GameHud_Internal.h — shared file-local constants/helpers for the GameHud.cpp
// split family (GameHud.cpp / GameHud_Vitals.cpp / GameHud_Alliance.cpp /
// GameHud_Text.cpp / GameHud_Render.cpp). Anonymous-namespace constants/helpers used
// by more than one of these translation units, promoted to inline/constexpr content
// so each TU sees exactly one definition — same values and behavior as the single
// anonymous namespace this content originally lived in inside GameHud.cpp.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <d3d9.h>

#include "Game/ClientRuntime.h" // game::g_Client.VarGet (ComputeExpProgress)
#include "Game/GameDatabase.h"  // game::LevelInfo/GetLevelInfo/GetRebirthExpSpan (ComputeExpProgress)

namespace ts2::ui {

// --- Palette shared by DrawVitalsFrame / DrawBarFillQuantized / DrawAllianceFramePanels /
// DrawAllianceFrameText / DrawTextPass / Render (target plates) ------------------------
constexpr D3DCOLOR kBarBorder = 0xFF000000u; // bar outline
constexpr D3DCOLOR kHpBg      = 0xFF240808u; // HP bar background
constexpr D3DCOLOR kHpFill    = 0xFFC42828u; // HP fill (red)
constexpr D3DCOLOR kMpBg      = 0xFF08081Eu; // MP bar background
constexpr D3DCOLOR kMpFill    = 0xFF2A56C6u; // MP fill (blue)
constexpr D3DCOLOR kTextColor = 0xFFFFFFFFu; // primary text (white)
constexpr D3DCOLOR kTextDim   = 0xFFBFBFBFu; // secondary text (slot numbers)

constexpr int kMasteryMax = 3000; // push 0BB8h @0x67A737 ; dbl_7EDAE8 = 3000.0

// Discrete-step count shared by DrawAtlasBar (GameHud.cpp) and the DrawBarFillQuantized
// fallback calls in DrawVitalsFrame (GameHud_Vitals.cpp): the binary never computes a
// continuous ratio, only `Crt_ftol(cur*41.0/max)` among 41 steps (see DrawAtlasBar /
// DrawBarFillQuantized banners).
constexpr int kVitalsBarSteps = 41; // dbl_7A9860 = 41.0 (@0x67A451/0x67A504/0x67A649/0x67A6F6)

// --- Alliance/target-plate shared palette (§8 alliance frames + §7 target plates, same
// visual language reused by Render() for the two locked-target plates) -----------------
constexpr D3DCOLOR kAllyFrameBg     = 0xA0141420u; // row background (translucent)
constexpr D3DCOLOR kAllyFrameBrd    = 0xFF3A3A48u; // outline (same tint as the vitals frame)
constexpr D3DCOLOR kAllyIconOnline  = 0xFF3A8A46u; // presence dot (resolved, green)
constexpr D3DCOLOR kAllyIconOffline = 0xFF6A6A70u; // presence dot (not found, gray)
constexpr D3DCOLOR kAllyNoData      = 0xA0505058u; // "no known max" gauge (grayed out)
constexpr D3DCOLOR kAllyNameCol     = 0xFFE8E8F0u;
constexpr int kAllianceIconSize = 24;
constexpr int kAllianceBarH     = 9;

// Offsets of the 4 bar fields in the raw body (= global - dword_1687234 - 0x18, the body
// starting at entity+0x18). IDENTICAL to the B_288/B_HP/B_296/B_MP constants of
// Net/CharStatDeltaDispatch.cpp:76-79, the only server writer of these fields (op 0x11).
constexpr size_t kBodyHpMax = 288; // dword_168736C (players[0]+0x138) — fidiv @0x67A457
constexpr size_t kBodyHpCur = 292; // dword_1687370 (players[0]+0x13C) — fild  @0x67A44B
constexpr size_t kBodyMpMax = 296; // dword_1687374 (players[0]+0x140) — fidiv @0x67A50A
constexpr size_t kBodyMpCur = 300; // dword_1687378 (players[0]+0x144) — fild  @0x67A4FE

// Reads a signed int32 from an entity's raw body (PlayerEntity::body, entity+0x18). Same
// convention as Net/CharStatDeltaDispatch.cpp::RdI32/WrI32 and
// Game/AutoPlaySystem.cpp::ReadI32 (not exported, reimplemented locally).
inline int32_t RdBodyI32(const std::array<uint8_t, 600>& body, size_t off) {
    int32_t v = 0;
    std::memcpy(&v, body.data() + off, sizeof(v));
    return v;
}

// §1 EXP: exact progress/span extracted from UI_GameHud_Render 0x67A59E (fresh disasm
// verified this mission). Getters LevelTable_GetId 0x4C2930 -> LevelInfo::expCumul (+4),
// LevelTable_GetMinExp 0x4C2960 -> LevelInfo::expNext (+8) (decompiled: record = 11
// dwords, GetId=record[+1], GetMinExp=record[+2]). Read via g_Client.VarGet — NO
// SelfState field added (Game/GameState.h stays read-only, cf. task W4-F2).
inline void ComputeExpProgress(int level, int levelBonus, int& outProgress, int& outSpan) {
    if (levelBonus >= 1) {
        // Rebirth branch (`cmp g_SelfLevelBonus, 1 / jge` @0x67A597-0x67A59E -> 0x67A618).
        // span = maybe_GameHud_GetQuickSlotItemId(mLEVEL, g_SelfLevelBonus) @0x67A61F-0x67A624
        // = rebirth EXP sub-table (mLEVEL 0x8E7208 + 0x18EC, 12 int32 hardcoded by 0x4C2380)
        // -> ported by game::GetRebirthExpSpan (cf. Game/GameDatabase.cpp).
        outProgress = game::g_Client.VarGet(0x16731B4); // dword_16731B4 @0x67A62F
        outSpan     = game::GetRebirthExpSpan(levelBonus); // @0x67A624 / accessor 0x4C2BF0
    } else if (const game::LevelInfo* li = game::GetLevelInfo(level)) {
        const int expCumul = li->expCumul;                          // GetId(level) 0x4C2930 = +4
        outProgress = game::g_Client.VarGet(0x16731B0) - expCumul;  // dword_16731B0 - GetId @0x67A608
        // level<145 (0x91): span = GetMinExp - GetId; otherwise 2e9 (0x77359400) - GetId.
        outSpan = (level < 145 ? li->expNext : 2000000000) - expCumul; // @0x67A5B8 / 0x67A5EA
    } else {
        outProgress = 0;
        outSpan     = 0;
    }
}

} // namespace ts2::ui

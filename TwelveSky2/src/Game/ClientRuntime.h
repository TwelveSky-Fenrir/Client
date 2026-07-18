// Game/ClientRuntime.h — shared CLIENT runtime state (network handler + UI hub).
//
// Common foundation for the inbound packet application modules
// (Net/GameHandlers_*.cpp) and the UI (HUD, inventory, chat). Clean C++
// rewrite of the binary's hundreds of scattered globals (g_InvMain,
// g_Currency, g_InvWeight, boss bars, message log, active dialogs…). See
// RE/net_handler_notes.md for the semantics of the original handlers,
// Docs/TS2_GAMEPLAY_LOGIC.md and Docs/TS2_CLIENT_SHELL.md.
//
// GOLDEN RULE (parallelization): this file is the FOUNDATION. Handler
// modules do NOT EDIT it — they include it and use its API. A long-tail
// global (dword_XXXX) not modeled here is faithfully stored via
// Var(originalAddress) without touching this header.
#pragma once
#include "Game/GameState.h"   // InvCell
#include <cstdint>
#include <array>
#include <deque>
#include <string>
#include <unordered_map>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Message log (chat + system + floating). Groups the binary's
// Msg_AppendSystemLine, Msg_AppendChatLine and HUD_ShowFloatingMessage into a
// buffer readable by the UI (ChatWindow / HUD). Colors are ARGB D3DCOLOR.
// ---------------------------------------------------------------------------
enum class MsgKind : uint8_t { System = 0, Chat = 1, Whisper = 2, Faction = 3, Floating = 4 };

struct MessageLine {
    std::string text;
    uint32_t    color = 0xFFFFFFFFu;
    MsgKind     kind  = MsgKind::System;
    int         floatType = 0;   // HUD_ShowFloatingMessage: visual type (0..2)
    std::string who;             // sender (chat/whisper), empty otherwise
};

class MessageLog {
public:
    static constexpr size_t kMaxLines = 256;

    void System (const std::string& t, uint32_t color = 0xFFFFFFFFu);
    void Chat   (const std::string& t, uint32_t color, const char* who = nullptr);
    void Whisper(const std::string& t, const char* who);
    void Faction(const std::string& t, uint32_t color, const char* who = nullptr);
    void Floating(int floatType, int flag, const std::string& t, uint32_t color = 0xFFFFFFFFu);
    // HUD_ShowFloatingMessage(floatType, flag, text) — centered floating banner.
    //
    // Animation — VERIFIED in the binary (2026-07-14): NONE either.
    // HUD_ShowFloatingMessage 0x5AEEC0 just timestamps the slot
    // (this+4*type+1476 = g_GameTimeSec) and plays a sound; HUD_RenderFloatingMessages
    // 0x5AF4C0 draws each active slot at a FIXED position per type (hardcoded
    // coordinate table, switch(i)) at full opacity as long as
    // `g_GameTimeSec - timestamp <= 10.0`; past that delay the slot is zeroed
    // and disappears instantly (no fade-out). No lerp/easing/alpha ramp or
    // position slide over the 10s display: appearance and disappearance are
    // hard cuts (show/hide via flag + timer), not transitions. If this HUD
    // rendering is implemented on the ClientSource side, stay faithful to
    // this behavior (do not add a fade/slide that doesn't exist originally).

    const std::deque<MessageLine>& Lines() const { return lines_; }
    const MessageLine* Last() const { return lines_.empty() ? nullptr : &lines_.back(); }
    size_t Count() const { return lines_.size(); }
    void Clear() { lines_.clear(); }

private:
    void Push(MessageLine&& l);
    std::deque<MessageLine> lines_;
};

// ---------------------------------------------------------------------------
// Inventory grid (original SoA g_InvMain/g_InvGrid_* merged into AoS).
// Faithful addressing: the binary indexes [384*row + 6*col] (6 dwords/cell,
// row stride 384 dwords -> 64 columns/row). (row, col) -> cell reproduced here.
// ---------------------------------------------------------------------------
struct InventoryState {
    static constexpr uint32_t kCols = 64;   // 384/6
    static constexpr uint32_t kRows = 32;   // margin (bag pages + working warehouse)

    std::array<InvCell, kCols * kRows> cells{};

    int64_t currency = 0;   // g_Currency 0x1673180 (+ mirror dword_1687254[0])
    int64_t weight   = 0;   // g_InvWeight
    // Pending "aux" move cell (g_InvAux + dword_1674ABC/AC0).
    uint32_t aux0 = 0, aux1 = 0, aux2 = 0;

    InvCell& At(uint32_t row, uint32_t col) {
        const uint32_t r = (row < kRows) ? row : (kRows - 1);
        const uint32_t c = (col < kCols) ? col : (kCols - 1);
        return cells[r * kCols + c];
    }
    void Set(uint32_t row, uint32_t col, uint32_t itemId, uint32_t gridX,
             uint32_t gridY, uint32_t count, uint32_t durability, uint32_t serial) {
        InvCell& e = At(row, col);
        e.itemId = itemId; e.gridX = gridX; e.gridY = gridY;
        e.flag = count; e.color = durability; e.durability = serial;
    }
    void ClearCell(uint32_t row, uint32_t col) { At(row, col) = InvCell{}; }
    void Reset() { cells.fill(InvCell{}); currency = weight = 0; aux0 = aux1 = aux2 = 0; }
};

// ---------------------------------------------------------------------------
// Boss health bar (Net_OnBossHpInit / BossHpBarUpdate / BossHpPercent).
// The server sends DOUBLE the percentage (the client stores hp/2).
// ---------------------------------------------------------------------------
struct BossBar {
    bool     active   = false;
    int      percent  = 0;    // 0..100 (= received value / 2)
    uint32_t a = 0, b = 0, c = 0, d = 0;
    std::array<uint8_t, 420> panel{};  // panel body (Net_OnBossPanelLoad)
};

// ---------------------------------------------------------------------------
// Active dialog/prompt modal state (original dword_1822440/1822450 registry).
// active=false -> no dialog; type = prompt identifier (10/14/19/20…).
//
// Appear/disappear animation — VERIFIED in the binary (2026-07-14): NONE.
// UI_NoticeDlg_Open 0x5C0280 just sets flags/booleans; UI_NoticeDlg_Render
// 0x5C0630 draws the panel every frame at a FIXED position (centered via
// nWidth/nHeight, no time-dependent offset) at full opacity (Sprite2D_Draw
// with no alpha parameter) as long as the active flag is true;
// UI_NoticeDlg_OnLButtonUp 0x5C03F0 calls UI_NoticeDlg_Close (flag=0) and
// executes the action IMMEDIATELY on the same frame — no fade-out, no delay.
// There is no lerp, easing, or alpha/position ramp in these three functions:
// it's a binary show/hide via a simple flag test. PromptState's static
// rendering (LoginScene::RenderNotice) is therefore ALREADY faithful; do not
// add a fade/slide to it that doesn't exist in the original.
// ---------------------------------------------------------------------------
struct PromptState {
    bool     active = false;
    int      type   = 0;      // dword_1822450
    std::string title;
    std::string body;
    std::string name;         // injected name ([%s])
    void Open(int t, std::string b, std::string ttl = {}) {
        active = true; type = t; body = std::move(b); title = std::move(ttl);
    }
    void CloseIf(int t) { if (active && type == t) { active = false; type = 0; } }
    void Close() { active = false; type = 0; }
};

// ---------------------------------------------------------------------------
// ClientRuntime — runtime state hub (g_Client singleton). Complements
// game::g_World (entities + self + .IMG bases) with the UI/inventory/social/
// boss state driven by packet handlers.
// ---------------------------------------------------------------------------
class ClientRuntime {
public:
    InventoryState        inv;
    MessageLog            msg;
    std::array<BossBar, 2> boss{};
    PromptState           prompt;

    // Pending item-move cursor (g_PendingMove_SrcRow0 / dword_1822EF0 + item
    // snapshot dword_1822F08..). -1 = no move in progress.
    int32_t  pendingMoveRow = -1;
    int32_t  pendingMoveCol = -1;
    InvCell  pendingItem{};

    // Faithful escape hatch for the long tail of scalar globals (dword_XXXX,
    // word_XXXX, flt_XXXX) not modeled as a proper field. Key = original
    // address.
    //   E.g.: g_Client.Var(0x1675E9C) = pct;  // dword_1675E9C (boss hp %)
    int32_t&  Var(uint32_t addr) { return vars_[addr]; }
    int32_t   VarGet(uint32_t addr) const {
        auto it = vars_.find(addr); return it == vars_.end() ? 0 : it->second;
    }
    float&    VarF(uint32_t addr) {
        // reinterprets the int slot as float (same 4 bytes, like the binary).
        return *reinterpret_cast<float*>(&vars_[addr]);
    }

    // Complementary escape hatch for LARGE raw long-tail blobs (faithful
    // Crt_Memcpy copies, internal structure not decoded/not modeled as a
    // proper field — e.g. UI table dword_1686F74, leaderboard body
    // dword_1825E70). Key = original address; the size is fixed by the first
    // caller and checked on subsequent calls (silent failure -> returns the
    // existing buffer as-is rather than resizing it, so as to never
    // desynchronize a reader that already holds a reference). Usage:
    // g_Client.Blob(addr, n) = data.
    std::vector<uint8_t>& Blob(uint32_t addr, size_t size) {
        auto& b = blobs_[addr];
        if (b.empty()) b.assign(size, 0);
        return b;
    }

    void Reset() { inv.Reset(); msg.Clear(); boss = {}; prompt.Close();
                   pendingMoveRow = pendingMoveCol = -1; pendingItem = InvCell{};
                   vars_.clear(); blobs_.clear(); }

private:
    std::unordered_map<uint32_t, int32_t> vars_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> blobs_;
};

// Single global instance (clean mirror of the binary's global sprawl).
inline ClientRuntime g_Client;

// ---------------------------------------------------------------------------
// StrTable005_Get(id): the client resolves localized strings from a table
// (001.DAT / StrTable005). Since that table isn't decrypted here, a STABLE
// "#<id>" placeholder is returned so handlers stay faithful to the logic
// (which message, with which arguments) without depending on language
// assets.
std::string Str(int id);

} // namespace ts2::game

// Game/GuildSystem.h — Guild system: internal roster (ts2::game).
//
// CLEAN rewrite (not byte-exact) of the guild roster found in the disassembly
// of TwelveSky2.exe (imagebase 0x400000). Source of truth = the DISASM (MCP idaTs2) +
// RE/net_handler_notes.md (§ Net_OnGuildRosterUpdate 0x4f / Net_OnGuildRosterReset 0x4a /
// Net_OnTeamSlotAssign 0x56 / TeamFormationDispatch 0x53). Original object: g_Guild
// @ 0x1839968 (global struct WITHOUT a vtable — fields accessed via raw pointer
// arithmetic `*(this+N)` in the Hex-Rays decompile, no IDA type applied).
//
// Original functions reproduced here:
//   Guild_CountMembers          0x66BBC0  -> GuildRoster::CountMembers
//   Guild_SelectNextMember      0x66BC30  -> GuildRoster::SelectNextMember
//   Guild_AddMemberFromInput    0x66BCD0  -> GuildRoster::AddMember (state part);
//                                            network send (Net_SendOp76) wired on the
//                                            UI/GuildWindow.cpp::ConfirmAdd side
//   Net_OnTeamFormationDispatch 0x491E70 case 8  (@0x492874-0x492923, kick)
//                                            -> GuildRoster::RemoveMember(index)
//   Net_OnTeamFormationDispatch 0x491E70 case 17 (@0x492ded-0x492e2a, self-leave scan)
//                                            -> GuildRoster::RemoveMember(name)
//   UI_GuildMgrWnd_Open         0x667E20  (@0x667E29-0x667EB6, scan init)
//                                            -> GuildRoster::Reset
//
// LAYOUT INFERRED for g_Guild (0x1839968) — cross-decompilation of Guild_CountMembers
// (`*(this+429)`, `(char*)this+13*i+67`), Guild_SelectNextMember (`*(this+399)`,
// `*(this+430)`, Net_SendOp78), Net_OnTeamSlotAssign (`dword_1839EDC[dword_183A020]`,
// 0x493090), and UI_GuildMgrWnd_Open (`*(this+349..430)`, 0x667E20). Only the fields
// required by these functions are modeled; the rest (0..66, leader/co-leader @+28/+41/+54,
// UI selection grid dword_183A014/018 used by kick/promote in
// TeamFormationDispatch, 5-byte/member block unk_1839D00 @+920) is OUT OF SCOPE (TODO):
//
//   +0x43  (0x18399AB) unk_18399AB    : char name[50][13]  — NUL-terminated name, "" = empty slot
//                                        (compared against &String, empty string @0x7EC95F)
//   +0x2D0 (0x1839C38) dword_1839C38  : int32 rank[50]     — per-member rank (promote/kick/
//                                        leave, cf. TeamFormationDispatch case 8/9/17),
//                                        indexed like name[] (10*row+col == member index)
//   +0x574 (0x1839EDC) dword_1839EDC  : int32 slotValue[50]— value received sequentially
//                                        via Net_OnTeamSlotAssign (op 0x56), written at
//                                        index = current cursor (same i as name[i]);
//                                        sentinel -2 = "not yet requested" (Open init)
//   +0x63C (0x1839FA4) *(this+399)    : bool active        — "roster scan active",
//                                        set to 1 by UI_GuildMgrWnd_Open, gates
//                                        SelectNextMember (if false -> no request)
//   +0x6B4 (0x183A01C) *(this+429)    : int32 memberCount  — written by Guild_CountMembers
//   +0x6B8 (0x183A020) *(this+430)    : int32 cursor       — sequential scan cursor,
//                                        -1 = before the first; Net_OnTeamSlotAssign
//                                        uses it as-is as the index into slotValue[]
//
// DISTINCT roster (out of scope, cited to avoid confusion): g_AllianceRosterNames
// @ 0x16749B8 (5 slots: leader + 4 members, stride 13: 16749B8/C5/D2/DF/EC, plus
// g_LocalGuildName 0x168740C annex) handled by Net_OnGuildRosterReset (op 0x4A, 0x4911D0),
// Net_OnGuildRosterUpdate (op 0x4F, 0x4918D0), GuildMemberJoin/Leave/Kick (0x4B/0x4D/0x4E).
// NO relation to the 50-member roster modeled here — do not merge the two.
// Now ACTUALLY modeled on the ClientSource side: Game/GameState.h::AllianceRoster
// (game::g_World.allianceRoster), wired by Net/GameHandlers_PartyGuild.cpp ("ALLIANCE/GUILD
// ROSTER WIRING" mission, 2026-07-14, cf. Docs/TS2_ALLIANCE_PARTY_ROSTER.md §3).
#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace ts2::game {

// ---------------------------------------------------------------------------
// One member of the internal guild roster (fixed slot, index 0..49).
// ---------------------------------------------------------------------------
struct GuildMember {
    std::string name;              // unk_18399AB + 13*i ("" = empty slot)
    int32_t     rank      = 0;     // dword_1839C38[i]
    int32_t     slotValue = -2;    // dword_1839EDC[i] (-2 = not yet received, cf. UI Open)

    bool Empty() const { return name.empty(); }
};

// ---------------------------------------------------------------------------
// GuildRoster — mirror of the global object g_Guild (0x1839968). Single instance:
// ts2::game::g_Guild (declared at the bottom of this file).
// ---------------------------------------------------------------------------
class GuildRoster {
public:
    static constexpr int kMaxMembers = 50;   // bound of the binary's 3 `i < 50` loops
    static constexpr int kNameStride = 13;   // (char*)this + 13*i + 67

    std::array<GuildMember, kMaxMembers> members{};

    bool    active      = false; // *(this+399)
    int32_t memberCount = 0;     // *(this+429)
    int32_t cursor      = -1;    // *(this+430)

    // Guild_CountMembers 0x66BBC0 — recounts the slots whose name is non-empty
    // (Crt_Strcmp(name_i, "") != 0), writes memberCount. Returns the new value.
    int CountMembers();

    // Guild_SelectNextMember 0x66BC30 — if !active returns false without doing anything.
    // Otherwise scans from cursor+1 to 49 for the first non-empty slot: if found, sets
    // cursor=i and returns true (triggers the member-info request send on the binary
    // side); if none found, clamps cursor=49 (kMaxMembers-1) and returns false.
    // Network send (if return true -> Net_SendOp78(&unk_846C08, members[cursor].name)):
    // out of scope for GuildSystem (pure state module, no network dependency) — wired
    // on the caller side in Net/GameHandlers_PartyGuild.cpp (the TeamSlotAssign
    // handler 0x56 chains into g_Guild.SelectNextMember() after consuming the value
    // received in dword_1839EDC[cursor], then sends Net_SendOp78(members[cursor].name)
    // if the scan found a member).
    bool SelectNextMember();

    // Guild_AddMemberFromInput 0x66BCD0 — state/logic part only: the binary
    // reads an edit box (GetWindowTextA, UI out of scope), clears the box
    // (SetWindowTextA), then tests `name` against the banned-word dictionary
    // (maybe_Dict001_MatchWord 0x4C1410, out of scope): if banned -> system message
    // StrTable005(112) (no send); else -> Net_SendOp76(name) (sends the add
    // request). Here: `banned` is computed by the caller (UI + banned-word dictionary
    // not modeled in this module); does NOT modify `members` (the roster is only
    // updated on the server response, absent from the 3 target functions). Returns true
    // if the request should be sent (name non-empty and !banned).
    // If return true -> the caller sends Net_SendOp76(&unk_846C08, name)
    // (wired on the UI/GuildWindow.cpp::ConfirmAdd side, cf. Docs/TS2_SENDPACKETS_USAGE_AUDIT.md).
    bool AddMember(const std::string& name, bool banned) const;

    // Derived from Net_OnTeamFormationDispatch (0x491E70) case 8 [kick, @0x492874-0x492923]:
    // clears name[index] and resets rank[index]=0 (the binary also clears a 5-byte
    // side field unk_1839D00, out of scope), then recounts (CountMembers). slotValue[index]
    // is NOT touched by the original (dword_1839EDC does not appear on this path).
    // No-op if index is outside [0, kMaxMembers).
    void RemoveMember(int index);

    // Name-based variant, derived from Net_OnTeamFormationDispatch case 17 (self-leave,
    // @0x492ded-0x492e2a): linear scan `i=0..49` for the first name[i]==name.
    // FIDELITY NOTE: the original does NOT test whether the search failed and writes
    // dword_1839C38[50] unguarded when name is absent from the roster (a 1-element
    // array overrun, probably harmless since it falls into the adjacent unk_1839D00)
    // — this behavior is NOT reproduced here: if `name` is not found,
    // this function does nothing (deliberate safety guard, documented gap).
    void RemoveMember(const std::string& name);

    // Derived from UI_GuildMgrWnd_Open (0x667E20 @0x667E29-0x667EB6): resets the
    // scan state — slotValue[i]=-2 for i=0..49, active=true, cursor=-1.
    // Does NOT touch name[]/rank[] (filled elsewhere by copying the 1388-byte guild
    // "blob" received via TeamFormationDispatch before the original call) and does NOT
    // call CountMembers()/SelectNextMember() (the original chains them right after, to
    // be done from the caller as in UI_GuildMgrWnd_Open).
    void Reset();
};

// Single global instance (mirror of g_Guild 0x1839968).
inline GuildRoster g_Guild;

} // namespace ts2::game

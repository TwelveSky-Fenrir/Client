// Net/GameHandlers_PartyGuild.cpp — routes PARTY / GUILD / ALLIANCE / TEAM packets.
//
// "party_guild" domain (RE/handler_domains.json): invitations (party/alliance),
// roster (members, add/remove/kick), guild/faction chat, values/positions/HP
// of members. Original semantics: RE/net_handler_notes.md.
//   0x2e PartyInvitePrompt   0x2f PartyInviteDecline  0x30 PartyJoinResult
//   0x34 AllyInvitePrompt    0x35 AllyInviteDecline   0x36 AllyJoinResult
//   0x37 GuildMemberInfo     0x38 GuildInfoUpdate     0x3d PartyResultDialog
//   0x3e PartyMemberNameSet  0x3f PartyMemberValueSet  0x40 PartyMemberClear
//   0x4a GuildRosterReset    0x4b GuildMemberJoin     0x4c GuildChatMessage
//   0x4d GuildMemberLeave    0x4e GuildMemberKick     0x4f GuildRosterUpdate
//   0x53 TeamFormationDispatch 0x54 GuildNoticeChat   0x56 TeamSlotAssign
//   0x5c GuildActionResult   0x5d PartyInviteResult   0x7b PartyMemberTargetSet
//   0x7f PartyMemberHpSet    0x80 PartyMemberUpdate   0x81 PartyItemResult
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // net::g_GmCmdCooldownLatch
#include "Net/SendPackets.h"
#include "Config/GameOptions.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"    // game::g_World (entity resolution by network identity)
#include "Game/GuildSystem.h"  // game::g_Guild (Guild_SelectNextMember)
#include "Game/StatEngine.h"   // game::StatEngine::CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0)
#include "Game/MapWarp.h"      // game::BeginWarpToFactionTown (warp resolution, not the send)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (real coordinates 003.BIN)
// FIRST Net -> UI edge in the repo (until now Net/ included NO UI/ header). Introduced
// deliberately to wire ONE precise anchor: Net_OnPartyMemberNameSet 0x4909A0 calls
// UI_MemberSelectWnd_Open @0x4909F8 WITHOUT ANY GUARD (the arena guard is *inside* _Open,
// @0x66726E). This is the TRIGGER for the whole member-selector chain: without it, the
// Op57/Op58 sends from UI/PartyWindow remain dead code (cf. UI/PartyWindow.h:71-83).
// Placed AFTER Net/SendPackets.h -> Net/NetClient.h, which pulls <winsock2.h> BEFORE <windows.h>
// (UI/GameWindows.h transitively pulls <windows.h> via UIManager.h -> <d3d9.h>): the project's
// Winsock ordering is therefore preserved.
#include "UI/GameWindows.h"                // ts2::ui::GameWindows::Instance()->Party()
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

namespace {

// Chat channel colors (faithful placeholders for the binary's g_ChatColor_Guild 0x84DFE8 /
// g_ChatColor_Faction 0x84DFEC; exact D3DCOLOR values to be re-extracted).
// Verified by decompilation+xrefs idaTs2 (2026-07-14, cf. GameHandlers_ChatSocial.cpp):
// these globals are 0 in .data and have no write site in the binary —
// the real value is set outside the static binary (config/runtime skin), not an oversight.
constexpr uint32_t kChatColorGuild   = 0xFF66FF66u; // g_ChatColor_Guild
constexpr uint32_t kChatColorFaction = 0xFFFFCC33u; // g_ChatColor_Faction

// Reads a char[N] (13 bytes) NUL-terminated field into a clean std::string.
template <size_t N>
inline std::string Name(const char (&s)[N]) {
    return std::string(s, ::strnlen(s, N));
}

// Resolves a player entity index by network identity (idHi,idLo) WITHOUT adding it
// (the original handler only does a linear scan: dword_1687238/168723C[227*e]).
// index 0 = self. Returns -1 if absent.
int FindPlayerIndex(uint32_t hi, uint32_t lo) {
    auto& players = ts2::game::g_World.players;
    for (size_t i = 0; i < players.size(); ++i)
        if (players[i].active && players[i].id.hi == hi && players[i].id.lo == lo)
            return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// "local player affiliation identity" block — 4 name buffers of 13 bytes that the
// binary ALWAYS writes TOGETHER: here in 0x53 cases 1 (0x491F36..0x491F87) / 4
// (0x492442..0x49248C) / 6 (0x492668..0x4926B2), and 0x5E cases 102/107 (on the
// Net/WorldEntityDispatch.cpp side). RE-PROVEN in IDA (wave W11 / WARP-03).
//
// FALSE FRIEND DISPELLED — `Crt_StringInit 0x75CAB0` is NOT a "std::string
// constructor" (as the IDB comment claims, and as the TODOs below used to repeat):
// it's `strcpy(dest, src)`. Proof — it's the alternate entry point of
// `Crt_Strcat 0x75CAC0` that SKIPS the end-of-string scan, both sharing the same
// copy body:
//     0x75CAB0  push edi
//     0x75CAB1  mov  edi, [esp+4+arg_0]   ; edi = dest AS-IS (no scan)
//     0x75CAB5  jmp  loc_75CB25           ; -> COMMON body
//     0x75CB25  mov  ecx, [esp+4+arg_4]   ; ecx = src, then copy loop src -> edi
// (strcat, on the other hand, reaches 0x75CB25 via `lea edi,[ecx-1]` @0x75CB13 = END of dest.)
// So: 2 arguments (dest, src), and the call COPIES — these are neither "init" nor
// "clear" calls. `offset String` 0x7EC95F has byte 0 at 0x00 -> empty string, so
// "strcpy(dest, String)" is the real clear (same pattern as GameHandlers_ChatSocial.cpp).
//
// TWO DISTINCT STORES — this isn't an aesthetic choice, each has its own proof:
//   • 0x16746A8 / 0x16746BC: STANDALONE globals -> `g_Client.Blob` (long tail).
//     0x16746A8 has a LIVE reader: UI/ClanContextMenu.cpp:550 `BlobNonEmpty(kVarGuildTag)`
//     -> `Blob(0x16746A8, 13)`. The key size 13 is MANDATORY: `Blob` locks the size at the
//     FIRST caller (ClientRuntime.h:179-183, `if (b.empty()) b.assign(size,0)`) — opening
//     this key at 16 elsewhere would silently overflow the heap. The binary's slot is 16 bytes
//     (0x16746B8 - 0x16746A8), but the WIRE field is 13 (T1/T2 strides of 0x5E) -> 13 everywhere.
//   • 0x168725C / 0x1687270: NOT standalone globals. These are fields of
//     g_EntityArray 0x1687234 (stride 908, index 0 = self — GameState.h:122):
//         0x168725C = entity[0]+40 = body+16   (body starts at entity+0x18)
//         0x1687270 = entity[0]+60 = body+36
//     Their MODELED home is therefore `g_World.players[0].body`, and body+16 has TWO
//     live readers: Scene/WorldRenderer.cpp:803 (`ReadBodyCString(p.body, 40-24, …)` ->
//     actor.affiliationName, name plate) and World/TerrainPicker.cpp:280. Storing them as
//     `Blob(0x168725C)` would create a SECOND store that NO ONE reads — exactly the
//     "produced-but-never-consumed" defect this campaign hunts down. -> write into the body instead.
//
// STRUCTURAL CORROBORATION (the two groups are EXACT mirrors):
//     0x16746A8 (name, 16 bytes) | 0x16746B8 (scalar) | 0x16746BC (name)
//     entity+40 (name, 16 bytes) | entity+56 (scalar) | entity+60 (name)
//   identical +16 / +20 gaps on both sides -> 0x16746A8 is the scalar mirror of
//   self's affiliation, copied into self's entity record for display.
// ---------------------------------------------------------------------------
constexpr uint32_t kLocalAffilName  = 0x16746A8; // dword_16746A8 (= UI/ClanContextMenu::kVarGuildTag)
constexpr uint32_t kLocalAffilName2 = 0x16746BC; // unk_16746BC
constexpr size_t   kSelfBodyAffil   = 40 - 24;   // byte_168725C (= WorldRenderer::kNpBodyAffiliation)
constexpr size_t   kSelfBodyAffil2  = 60 - 24;   // unk_1687270

// Faithful `strcpy(dest, src)` into a 13-byte long-tail name buffer (Blob store).
// ASSUMED DEVIATION vs the binary: strcpy leaves the TAIL of the destination intact
// (leftover from the old name), we zero-fill it. Not observable — every reader of these
// buffers is a strcmp/%s that stops at the 1st NUL; and the zero-fill guarantees NUL
// termination where the binary relies on a NUL present in its source.
void SetBlobName13(uint32_t addr, const char* src) {
    auto& b = ts2::game::g_Client.Blob(addr, 13);
    size_t n = 0;
    while (n < 13 && src[n] != 0) ++n;   // strcpy: stop at 1st NUL
    b.assign(13, 0);
    std::memcpy(b.data(), src, n);
}

// `strcpy(dest, src)` into a name field of SELF's entity record (g_EntityArray[0]).
// Guard `!players.empty()`: same convention as App/App.cpp:1161-1165 — we do NOT
// fabricate a ghost self (cf. the warning in App.cpp:770; `g_World.Self()` would
// emplace_back), where the binary writes into a static array that's always present.
void SetSelfBodyName13(size_t bodyOffset, const char* src) {
    auto& players = ts2::game::g_World.players;
    if (players.empty()) return;
    auto& body = players[0].body;
    if (bodyOffset + 13 > body.size()) return;
    size_t n = 0;
    while (n < 13 && src[n] != 0) ++n;
    std::memset(body.data() + bodyOffset, 0, 13);
    std::memcpy(body.data() + bodyOffset, src, n);
}

// Shared guild-state-reset block, IDENTICAL between Net_OnTeamFormationDispatch
// (0x491E70) case 4 (0x492442-0x4924F3) and case 6 (0x492668-0x492719) — same writes, same
// order, only the final message differs (478 vs 470). Factored out here because the binary
// literally duplicates the sequence (this is not a refactor: both sites are identical).
void ResetGuildStateBlock() {
    auto& c = ts2::game::g_Client;
    // The 4 leading strcpy(dest, "") — cf. banner above (ex-TODO "buffers not
    // modeled": resolved, the store already existed on both sides).
    SetBlobName13(kLocalAffilName,  "");   // 0x492442 / 0x492668
    SetBlobName13(kLocalAffilName2, "");   // 0x49245E / 0x492684
    SetSelfBodyName13(kSelfBodyAffil,  ""); // 0x492470 / 0x492696
    SetSelfBodyName13(kSelfBodyAffil2, ""); // 0x49248C / 0x4926B2
    c.Var(0x16746B8) = 0;   // 0x49244A / 0x492670
    c.Var(0x168726C) = 0;   // 0x492478 / 0x49269E
    c.Var(0x1687450) = 0;   // 0x492494 / 0x4926BA
    c.Var(0x168744C) = 0;   // 0x49249E / 0x4926C4
    c.Var(0x1675664) = 0;   // 0x4924A8 / 0x4926CE — dword_1675664[0] (index 0, NOT [slot])
    c.Var(0x1675660) = 0;   // 0x4924B2 / 0x4926D8
    c.Var(0x1675668) = 0;   // 0x4924BC / 0x4926E2
    c.Var(0x167566C) = 0;   // 0x4924C6 / 0x4926EC
    // Char_CalcAttackRatingMin/Max(g_EquipSnapshotScratch): g_EquipSnapshotScratch (0x8E719C)
    // = self's snapshot -> StatEngine facade over g_World.self/db (established and
    // documented precedent: Net/CharStatDeltaDispatch.cpp:144, Net/GameHandlers_Misc.cpp:79).
    c.Var(0x168736C) = ts2::game::StatEngine::CalcAttackRatingMin(   // 0x4924DA / 0x492700
        ts2::game::g_World.self, ts2::game::g_World.db);
    c.Var(0x1687374) = ts2::game::StatEngine::CalcAttackRatingMax(   // 0x4924E9 / 0x49270F
        ts2::game::g_World.self, ts2::game::g_World.db);
    // TODO(ui) [0x4924F3 / 0x492719] UI_RemoveActiveBuffSlot (0x55D5B0) — UI not owned here.
}

// Closes the modal notice (original dword_18225D0/18225D8 registry, distinct from
// the msgbox g_Client.prompt = dword_1822440/1822450). Modeled via Var for fidelity.
void CloseNotice(int type) {
    auto& c = ts2::game::g_Client;
    if (c.VarGet(0x18225D0) && c.VarGet(0x18225D8) == type)
        c.Var(0x18225D0) = 0;
}

} // namespace

void RegisterPartyGuildHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, g_World, Str()

    // 0x2e PartyInvitePrompt — party invite: opens the yes/no box (type 8) if the
    // filter is active; otherwise auto-declines (Net_SendOp45(2), faithful to Pkt_PartyInvitePrompt).
    OnPacket<PartyInvitePrompt>(sys, 0x2e, [&sys](const PartyInvitePrompt& p) {
        const std::string nm = Name(p.inviterName);
        if (config::g_Options.FilterPartyInvite) {
            g_Client.prompt.Open(8, "[" + nm + "]" + Str(p.flag == 1 ? 305 : 426), Str(306));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp45(sys.Client(), 2);
            g_Client.msg.System(Str(304));
        }
    });

    // 0x2f PartyInviteDecline — party invite declined: closes prompt 8 + str307.
    OnTrigger(sys, 0x2f, []() {
        g_Client.prompt.CloseIf(8);
        g_Client.msg.System(Str(307));
    });

    // 0x30 PartyJoinResult — party-join result: closes notice 4, str308..313.
    // The jpt_48FC18 jump table (Pkt_PartyJoinResult 0x48FBD0) covers EXACTLY cases 0..5
    // (case 0 = str 0x134 = 308 at 0x48FC25) and its `default` returns without displaying
    // anything: a code > 5 must therefore be SILENT, not show an arbitrary Str() from the
    // neighboring module.
    OnPacket<PartyJoinResult>(sys, 0x30, [&sys](const PartyJoinResult& p) {
        CloseNotice(4);
        if (p.resultCode <= 5) // 0x48FC18 (6-case jump table); default -> no message
            g_Client.msg.System(Str(308 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0)  // 0x48FC44: join confirmed -> requests party info (opcode 0x2E, no payload).
            Net_SendOp46(sys.Client());
    });

    // 0x34 AllyInvitePrompt — alliance invite: remembers the inviter + prompt (type 9)
    // if the filter is active; otherwise auto-declines (Net_SendOp49(2)).
    OnPacket<AllyInvitePrompt>(sys, 0x34, [&sys](const AllyInvitePrompt& p) {
        const std::string nm = Name(p.name);
        if (config::g_Options.FilterAllyInvite) {
            g_Client.Var(0x1822838) = static_cast<int32_t>(p.inviterId); // dword_1822838 = inviterId
            g_Client.prompt.Open(9, "[" + nm + "]" + Str(325), Str(326));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp49(sys.Client(), 2);
            g_Client.msg.System(Str(324));
        }
    });

    // 0x35 AllyInviteDecline — alliance invite declined: closes prompt 9 + str327.
    OnTrigger(sys, 0x35, []() {
        g_Client.prompt.CloseIf(9);
        g_Client.msg.System(Str(327));
    });

    // 0x36 AllyJoinResult — alliance-join result: closes notice 5, str328..333/2237.
    // The jpt_490138 jump table (Pkt_AllyJoinResult 0x4900F0) covers EXACTLY cases 0..6:
    // str 328..333 for 0..5 (case 0 = 0x148 = 328 at 0x490145) and str 2237 (0x8BD at 0x490252)
    // for the SINGLE case 6; `default` is silent. The old form `code <= 5 ? ... : Str(2237)`
    // displayed 2237 for EVERY code >= 6, and — `code` being an int derived from a uint32_t —
    // resultCode=0xFFFFFFFF gave code=-1, passed `code <= 5` and read Str(327).
    // Explicit switch on the UNSIGNED value, modeled after the jump table.
    OnPacket<AllyJoinResult>(sys, 0x36, [&sys](const AllyJoinResult& p) {
        CloseNotice(5);
        switch (p.resultCode) { // 0x490138 (7-case jump table)
            case 0: case 1: case 2: case 3: case 4: case 5:
                g_Client.msg.System(Str(328 + static_cast<int>(p.resultCode)));
                break;
            case 6: g_Client.msg.System(Str(2237)); break; // 0x49024B (0x8BD)
            default: break;                                // silent, faithful to default
        }
        if (p.resultCode == 0) {
            // 0x49015F-0x490169: `push offset unk_182296C` (src) / `push offset unk_1822828`
            // (dest) / `call Crt_StringInit` = COPY 0x182296C -> 0x1822828, NOT a reset
            // to empty (the pseudocode shows "Crt_StringInit()" with no argument, hence the
            // previous misreading: `Blob(0x1822828,1).assign(1,0)`).
            // DELIBERATELY NOT REPRODUCED: 0x1822828 is a WRITE-ONLY global across the whole
            // binary — xrefs_to = 2, both of them WRITES (0x49002B Pkt_AllyInvitePrompt,
            // 0x490164 here), NO reader. Modeling the copy would have no observable effect
            // and 0x182296C isn't modeled on the C++ side; we document instead of inventing.
            g_Client.Var(0x1822838) = g_Client.VarGet(0x1822984) + g_Client.VarGet(0x1822980) +
                                       g_Client.VarGet(0x182297C); // 0x490171-0x490183
            Net_SendOp50(sys.Client());  // requests the alliance roster (opcode 0x32, no payload).
        }
    });

    // 0x37 GuildMemberInfo — guild member info/list block: str334.
    OnPacket<GuildMemberInfo>(sys, 0x37, [](const GuildMemberInfo&) {
        g_Client.msg.System(Str(334));
        // TODO(ui): UI_ItemListWin_Open(field0, blockA[128], blockB[96], field228)
        //   — decode member names/stats.
    });

    // 0x38 GuildInfoUpdate — guild info + roster update: header/footer + 8 members {id,val1,val2}.
    OnPacket<GuildInfoUpdate>(sys, 0x38, [](const GuildInfoUpdate& p) {
        g_Client.Var(0x1822848) = static_cast<int32_t>(p.header); // dword_1822848
        g_Client.Var(0x1822934) = static_cast<int32_t>(p.footer); // dword_1822934
        for (int i = 0; i < 8; ++i) {                             // 8 members x 12 bytes
            uint32_t id = 0, v1 = 0, v2 = 0;
            std::memcpy(&id, p.members + 12 * i + 0, 4);
            std::memcpy(&v1, p.members + 12 * i + 4, 4);
            std::memcpy(&v2, p.members + 12 * i + 8, 4);
            g_Client.Var(0x18228CC + 12 * i) = static_cast<int32_t>(id); // dword_18228CC[3*i]
            g_Client.Var(0x18228D0 + 12 * i) = static_cast<int32_t>(v1); // dword_18228D0[3*i]
            g_Client.Var(0x18228D4 + 12 * i) = static_cast<int32_t>(v2); // dword_18228D4[3*i]
        }
        // Crt_Memcpy(dword_182284C, &v5, 0x80u) — 0x490417 (Pkt_GuildInfoUpdate 0x490360).
        // This 128-byte block is NOT a "guild name/notice" (a misreading of the original,
        // cf. field comment to fix RecvPackets.h:1111): it's a structured STAGING buffer,
        // proven by its sole consumer. xrefs_to(0x182284C) = exactly 2: the write here
        // (0x490412) and the read at 0x4905D2 in Net_OnFactionBoardSync (0x490560, opcode
        // 0x3a), which reads it back field by field (0x4905D8-0x490620) via dword_182284C[4*i],
        // dword_1822850[4*i], dword_1822854[4*i], dword_1822858[4*i] — i.e. 8 records
        // of 4 dwords, stride 16 bytes = exactly the 128 bytes. Same pattern as the 8-member
        // loop above, which already stages 0x18228CC/D0/D4 for this same consumer.
        for (int i = 0; i < 8; ++i) { // 8 x {4 dwords}, stride 16 bytes
            uint32_t a = 0, b = 0, c = 0, d = 0;
            std::memcpy(&a, p.block128 + 16 * i + 0,  4);
            std::memcpy(&b, p.block128 + 16 * i + 4,  4);
            std::memcpy(&c, p.block128 + 16 * i + 8,  4);
            std::memcpy(&d, p.block128 + 16 * i + 12, 4);
            g_Client.Var(0x182284C + 16 * i) = static_cast<int32_t>(a); // dword_182284C[4*i]
            g_Client.Var(0x1822850 + 16 * i) = static_cast<int32_t>(b); // dword_1822850[4*i]
            g_Client.Var(0x1822854 + 16 * i) = static_cast<int32_t>(c); // dword_1822854[4*i]
            g_Client.Var(0x1822858 + 16 * i) = static_cast<int32_t>(d); // dword_1822858[4*i]
        }
        // NOTE (wiring out of scope): this staging buffer is only OBSERVABLE if handler 0x3a
        // commits it (Net_OnFactionBoardSync 0x490560: 0x49059A/0x4905A5 + loop
        // 0x4905AA-0x490668). That handler lives in Net/GameHandlers_ChatSocial.cpp, not owned
        // by this front -> flagged in the report for routing (the "produced-but-never-
        // consumed" anti-pattern, assumed and tracked, not ignored).
    });

    // 0x3d PartyResultDialog (Net_OnPartyResultDialog 0x490800) — party action result:
    // closes notice 6, str492..497. Like 0x30/0x36, the switch is BOUNDED: 0x49083B
    // `cmp [ebp+var_C],5` / 0x49083F `ja def_490848` ("6-case switch", case 0 = 0x1EC = 492
    // at 0x490855) -> a code > 5 is SILENT. (Deviation not flagged by recon, found while
    // re-proving the scan below; same nature and same fix as case 0x30.)
    OnPacket<PartyResultDialog>(sys, 0x3d, [&sys](const PartyResultDialog& p) {
        CloseNotice(6);
        if (p.resultCode <= 5) // 0x49083B/0x49083F; default -> no message
            g_Client.msg.System(Str(492 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0) {
            // First EMPTY slot of g_PartyRosterNames -> requests its info (Net_SendOp56).
            // Polarity RE-PROVEN at DISASSEMBLY (2026-07-16) — it was INVERTED here:
            //   0x490887 `push offset String` (src = "" @0x7EC95F, byte 0 verified NUL)
            //   0x490892 `add edx, offset g_PartyRosterNames` (dest = names + 13*i)
            //   0x490899 `call Crt_Strcmp` / 0x4908A1 `test eax,eax`
            //   0x4908A3 `jnz short loc_4908A7` -> 0x4908A7 `jmp loc_490878` = INCREMENT
            // => strcmp != 0 (name NOT empty) CONTINUES the loop; the exit happens on
            //    strcmp == 0, i.e. at the 1st EMPTY slot.
            // Cross-proof (the two polarities are REALLY opposite in the binary, the C++ had
            // wrongly unified them): sibling handler 0x3f
            // (Net_OnPartyMemberValueSet 0x490A10) tests `jz loc_490A9B` (0x490A89) and
            // 0x490A9B = `jmp loc_490A5F` = increment -> there it's really the 1st non-empty
            // that triggers Net_SendOp57; the 0x3f handler below is correct, don't align it.
            auto& names = g_World.partyRoster.names;
            for (size_t i = 0; i < names.size(); ++i) { // 0x490881 `cmp var_4,0Ah` / jge
                if (names[i].empty()) {
                    Net_SendOp56(sys.Client(), static_cast<int8_t>(i));
                    break;
                }
            }
            // Full roster (i == 10): 0x4908A9 `cmp var_4,0Ah` / 0x4908AD `jnz loc_4908B4`
            // -> 0x4908AF `jmp def_490848` = NO send (the `break` above is never
            // taken, so no Net_SendOp56 — faithful without an extra guard).
        }
    });

    // 0x3e PartyMemberNameSet (Net_OnPartyMemberNameSet 0x4909A0) — sets a member's name
    // in a party roster slot (g_PartyRosterNames, cf. Game/GameState.h::PartyRoster).
    OnPacket<PartyMemberNameSet>(sys, 0x3e, [](const PartyMemberNameSet& p) {
        const std::string nm = Name(p.name);
        if (p.slotIndex < g_World.partyRoster.names.size())
            g_World.partyRoster.names[p.slotIndex] = nm;
        // 0x4909F8: `return UI_MemberSelectWnd_Open(g_MemberSelectWnd);` — last
        // instruction of the handler, UNCONDITIONAL (re-verified against decompiled output
        // 2026-07-16: the whole body is memcpy(index) / memcpy(name 13 bytes) / Crt_StringInit /
        // this call; NO guard). The arena guard (Str 1352) lives INSIDE _Open @0x66726E,
        // ported by PartyWindow::OpenMemberSelect — don't duplicate it here.
        // No system message from the original handler.
        // The instance is null outside the InGame scene (GameWindows is created/destroyed with
        // the HUD): the packet then simply has no UI effect, the roster write above having
        // already happened — same order as the binary (write THEN open).
        if (ts2::ui::GameWindows* gw = ts2::ui::GameWindows::Instance())
            gw->Party().OpenMemberSelect();
    });

    // 0x3f PartyMemberValueSet (Net_OnPartyMemberValueSet 0x490A10) — sets a member value
    // if the party is active, then rescans the name roster to paginate.
    OnPacket<PartyMemberValueSet>(sys, 0x3f, [&sys](const PartyMemberValueSet& p) {
        if (g_Client.VarGet(0x184BE40)) { // dword_184BE40 = party active
            g_Client.Var(0x184BE50 + 4 * static_cast<int>(p.index)) = static_cast<int32_t>(p.value);
            // Scans g_PartyRosterNames[index+1..9]; at the first non-empty name, requests the
            // next one (Net_SendOp57) — roster pagination (RE/net_handler_notes.md).
            auto& names = g_World.partyRoster.names;
            for (size_t i = static_cast<size_t>(p.index) + 1; i < names.size(); ++i) {
                if (!names[i].empty()) {
                    Net_SendOp57(sys.Client(), static_cast<int8_t>(i));
                    break;
                }
            }
        }
    });

    // 0x40 PartyMemberClear (Net_OnPartyMemberClear 0x490AB0) — clears a party roster
    // slot's name. Behavior CORRECT, but the old justification ("Crt_StringInit
    // with 1 argument in the binary = a real clear-to-empty") was WRONG: Crt_StringInit
    // is a strcpy and ALWAYS takes 2 arguments (the Hex-Rays pseudocode simply hides the
    // argument). This clear is real because the SOURCE pushed is `offset String`
    // (0x490AC7 = "" @0x7EC95F), the dest being g_PartyRosterNames + 13*idx (0x490AD2/0x490AD8).
    // It's THIS criterion — not an argument count — that distinguishes a clear from a copy.
    OnPacket<PartyMemberClear>(sys, 0x40, [](const PartyMemberClear& p) {
        if (p.slotIndex < g_World.partyRoster.names.size())
            g_World.partyRoster.names[p.slotIndex].clear();
    });

    // 0x4a GuildRosterReset (Net_OnGuildRosterReset 0x4911D0) — despite its name, this handler
    // resets NOTHING: it LOADS the alliance roster with the 5 payload names.
    // Proven at disassembly (2026-07-16): Crt_StringInit(arg1=dest, arg2=src) is a
    // strcpy — the Hex-Rays pseudocode shows it as "Crt_StringInit()" with no argument, which
    // had induced a bogus `Reset()` here. A real clear-to-empty is recognized by its
    // `push offset String` (0x7EC95F = empty string, verified: byte 0 is NUL); NONE of the 6
    // calls of 0x4a push it — these are 6 copies:
    //   0x49125B slot0 = name1 (payload+4)    0x49126C slot1 = name2 (payload+17)
    //   0x49127D slot2 = name3 (payload+30)   0x49128E slot3 = name4 (payload+43)
    //   0x49129F slot4 = name5 (payload+56)
    //   0x4912B1 g_LocalGuildName (0x168740C) = g_AllianceRosterNames[0] (0x16749B8)
    //            -> the founder/leader's name IS the guild name.
    // PLACEMENT: the 6 copies (0x491252-0x4912B6) precede the mode test
    // (0x4912B9 `mov edx,[ebp+var_58]` / 0x4912BF `cmp [ebp+var_5C],1`): they are therefore
    // UNCONDITIONAL and happen for ALL modes, including mode 3 and unknown modes
    // (0x4912CF `jz loc_491316` / 0x4912D1 `jmp loc_491316`: silent exit).
    OnPacket<GuildRosterReset>(sys, 0x4a, [](const GuildRosterReset& p) {
        auto& ar = g_World.allianceRoster;
        ar.memberNames[0] = Name(p.name1);  // 0x49125B
        ar.memberNames[1] = Name(p.name2);  // 0x49126C
        ar.memberNames[2] = Name(p.name3);  // 0x49127D
        ar.memberNames[3] = Name(p.name4);  // 0x49128E
        ar.memberNames[4] = Name(p.name5);  // 0x49129F
        ar.guildName      = ar.memberNames[0]; // 0x4912B1 (copy of slot 0, NOT a clear)
        // Slot by slot, no compaction: an empty payload name leaves the slot empty.
        if (p.mode == 1)      g_Client.msg.System(Str(349)); // 0x4912D3 (0x15D)
        else if (p.mode == 2) g_Client.msg.System(Str(881)); // 0x4912F5 (0x371)
        // mode 3 / default -> loc_491316: no message (faithful).
    });

    // 0x4b GuildMemberJoin (Net_OnGuildMemberJoin 0x491330) — new member: registered into
    // the 1st free slot of the alliance roster (slots 1..4, slot 0 = leader, never reassigned),
    // then announces "<str350> [name]<str351>".
    // The 0x491397 `if (i != 5)` guard wraps BOTH the slot write (0x4913AF) AND the
    // message (0x4913C6-0x491405): a full roster (i==5, loop 0x491359 `for i=1;i<5` exits
    // on the first empty slot via 0x491383 `Crt_Strcmp(&g_AllianceRosterNames[13*i], "")`) ->
    // NO mutation AND NO message. AddMember() returns precisely that boolean.
    OnPacket<GuildMemberJoin>(sys, 0x4b, [](const GuildMemberJoin& p) {
        const std::string nm = Name(p.name);
        if (g_World.allianceRoster.AddMember(nm)) // 0x491397: shared write+message guard
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(351));
    });

    // 0x4c GuildChatMessage — guild chat: "<str350> [sender] message".
    OnPacket<GuildChatMessage>(sys, 0x4c, [](const GuildChatMessage& p) {
        const std::string sender = Name(p.senderName);
        const std::string body   = std::string(p.message, ::strnlen(p.message, sizeof p.message));
        // Original: only posted if g_ChatShow_Guild==1 (flag not modeled -> always posted).
        g_Client.msg.Chat(Str(350) + " [" + sender + "] " + body, kChatColorGuild, sender.c_str());
    });

    // 0x4d GuildMemberLeave (Net_OnGuildMemberLeave 0x4914D0) — a member leaves the alliance:
    // if `nm` == the local name -> clears the whole roster (I myself left) + Str(882); otherwise
    // removes `nm` from the roster and compacts (cf. AllianceRoster::RemoveMember above, fidelity
    // note on the compaction algorithm). SelfState::localPlayerName is empty by default
    // (not populated by any handler so far) -> until it is, this branch is simply
    // never taken (honest degradation, cf. GameState.h comment).
    OnPacket<GuildMemberLeave>(sys, 0x4d, [](const GuildMemberLeave& p) {
        const std::string nm = Name(p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName) {
            g_World.allianceRoster.Reset();
            g_Client.msg.System(Str(882));
        } else if (g_World.allianceRoster.RemoveMember(nm)) { // 0x4915E7: shared guard
            // 0x4915E7 `if (i != 5)` wraps the compaction (0x4915FF + loop 0x49160D) AND the
            // message (0x491670-0x4916AF): name absent from roster -> no message.
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(352));
        }
    });

    // 0x4e GuildMemberKick (Net_OnGuildMemberKick 0x4916D0) — a member is kicked: same
    // logic as GuildMemberLeave above (self -> reset + Str(883); otherwise remove+compact).
    OnPacket<GuildMemberKick>(sys, 0x4e, [](const GuildMemberKick& p) {
        const std::string nm = Name(p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName) {
            g_World.allianceRoster.Reset();
            g_Client.msg.System(Str(883));
        } else if (g_World.allianceRoster.RemoveMember(nm)) { // 0x4917E7: shared guard
            // Same as 0x4d: 0x4917E7 `if (i != 5)` wraps compaction AND the str353 message.
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(353));
        }
    });

    // 0x4f GuildRosterUpdate (Net_OnGuildRosterUpdate 0x4918D0) — 3-case dispatcher.
    //   case 1 (0x491943): full reset (5 slots + guildName, all via `push offset String`),
    //     str350+" "+str354. Verified at disassembly — faithful.
    //   case 2 (0x491A00): message only str350+" "+str884 (0x374), NO mutation.
    //   case 3 (0x491A51): leader test `Crt_Strcmp(g_AllianceRosterNames[0], byte_1673184)`
    //     (0x491A5B) then `test eax,eax` / 0x491A65 `jnz loc_491B28`. REAL semantics,
    //     re-proven at disasm (2026-07-16) — the previous implementation had INVERTED it:
    //       strcmp==0  (slot 0 == my name -> I AM the leader): falls into 0x491A6B =
    //         full reset + str350+" "+str354 (0x162).  <- 354, NOT 700
    //       strcmp!=0  (I am NOT the leader): loc_491B28 = NO reset, but
    //         0x491B48 g_LocalGuildName = payloadName, then loop i=1..4
    //         (0x491B50-0x491BA6): 0x491B84 slot[i-1] = slot[i]; 0x491B9E slot[i] = ""
    //         => shifts DOWN by one, slot4 cleared; str350+" "+str700 (0x2BC).
    //     The write at 0x491B34 (slot0 = payloadName) is DEAD — immediately overwritten at i=1 by
    //     slot0 = slot1 — so not reproduced here.
    //   NUANCE (degenerate state, documented and NOT "fixed"): the binary does a raw strcmp,
    //     so two empty strings are equal -> reset branch. IsLeader() returns false
    //     on an empty name -> shift branch. Since SelfState::localPlayerName isn't populated by
    //     any handler so far, case 3 will ALWAYS take the non-leader branch. This is the
    //     most faithful degradation available (in the binary byte_1673184 is always
    //     populated, and a real non-leader player does take the shift branch); IsLeader() is
    //     kept by the convention "never a false I'm-the-leader".
    OnPacket<GuildRosterUpdate>(sys, 0x4f, [](const GuildRosterUpdate& p) {
        switch (p.code) {
            case 1: // 0x491943 — dissolution: resets the whole roster
                g_World.allianceRoster.Reset();
                g_Client.msg.System(Str(350) + " " + Str(354));
                break;
            case 2: // 0x491A00 — informational message only, no mutation
                g_Client.msg.System(Str(350) + " " + Str(884));
                break;
            case 3: {
                auto& ar = g_World.allianceRoster;
                if (ar.IsLeader(g_World.self.localPlayerName)) { // 0x491A5B strcmp==0 -> 0x491A6B
                    ar.Reset();                                  // 6x `push offset String`
                    g_Client.msg.System(Str(350) + " " + Str(354)); // 0x491AD7 (0x162)
                } else {                                         // 0x491A65 jnz -> loc_491B28
                    ar.guildName = Name(p.name);                 // 0x491B48
                    for (int i = 1; i < game::AllianceRoster::kMaxSlots; ++i) { // 0x491B50-0x491BA6
                        ar.memberNames[static_cast<size_t>(i - 1)] =
                            ar.memberNames[static_cast<size_t>(i)];             // 0x491B84
                        ar.memberNames[static_cast<size_t>(i)].clear();         // 0x491B9E
                    }
                    g_Client.msg.System(Str(350) + " " + Str(700)); // 0x491BA8 (0x2BC)
                }
                break;
            }
            default: // 0x4918D0: no other case -> silent exit
                break;
        }
    });

    // 0x53 TeamFormationDispatch (Net_OnTeamFormationDispatch 0x491E70) — guild
    // MEGA-DISPATCHER. statusCode (v88 @0x8156C1): 0 = success, >0 = error codes; subOpcode
    // (v86 @0x8156C5) selects the sub-handler; guildBlob 0x56C bytes @0x8156C9.
    // The master switch (0x491EED) handles EXACTLY 14 sub-opcodes:
    //   {1,2,3,4,5,6,7,8,9,0xA,0xC,0xD,0xE,0x11}. 0xB/0xF/0x10 fall through to `default` (no-op):
    //   their ABSENCE is therefore FAITHFUL, not an oversight.
    // g_GmCmdCooldownLatch = 0 is set IN EACH of the 14 cases (0x491EF4, 0x49200F, 0x492334,
    //   0x4923FD, 0x49256E, 0x492631, 0x4927BA, 0x49283C, 0x492979, 0x492AC0, 0x492BD6,
    //   0x492C20, 0x492C6A, 0x492D12) and NEVER on `default` — it is therefore moved into
    //   the cases (the old unconditional placement diverged on 0xB/0xF/0x10).
    // The global `TODO(msg)` is resolved: the StrTable005 ids below are individually taken
    //   from the decompiled output (EA cited). The 4 self affiliation-identity buffers
    //   (0x16746A8/0x16746BC/0x168725C/0x1687270) are now WIRED (case 1 + cases 4/6 via
    //   ResetGuildStateBlock; the store and the StringInit=strcpy false friend are documented in
    //   its banner, wave W11/WARP-03). Still OUT OF SCOPE (Game/GuildSystem.h, file not owned): the
    //   name-by-name roster (unk_1839D00/183A101, case 17 @0x492D57..) + the selection grid
    //   dword_183A014/018 + the rank dword_1839C38[].
    OnPacket<TeamFormationDispatch>(sys, 0x53, [&sys](const TeamFormationDispatch& p) {
        switch (p.subOpcode) { // 0x491EED
            case 1: // guild creation — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x491EF4
                if (p.statusCode == 0) {                     // 0x491F0E
                    // Sets self's affiliation identity from the blob. The 0x56C-byte blob is
                    // memcpy'd into var_580 @0x491EBA, and @0x491F2F `lea ecx,[ebp+var_580]` PROVES
                    // that the source is the blob at OFFSET 0 (no displacement): src = guildBlob+0.
                    // -> `strcpy(dword_16746A8, guildBlob)` @0x491F36 (affiliation name);
                    //    `strcpy(byte_168725C = entity[0]+40, guildBlob)` @0x491F6B (record mirror).
                    // The two "secondary" names (0x16746BC @0x491F57, 0x1687270 @0x491F87) are
                    // cleared (src = `offset String` 0x7EC95F, byte 0 = 0). Store and false-friend
                    // details (StringInit=strcpy): banner of ResetGuildStateBlock above.
                    SetBlobName13(kLocalAffilName,
                                  reinterpret_cast<const char*>(p.guildBlob));   // 0x491F36
                    g_Client.Var(0x16746B8) = 0;             // 0x491F43
                    SetBlobName13(kLocalAffilName2, "");                          // 0x491F57
                    SetSelfBodyName13(kSelfBodyAffil,
                                      reinterpret_cast<const char*>(p.guildBlob)); // 0x491F6B
                    g_Client.Var(0x168726C) = 0;             // 0x491F73
                    SetSelfBodyName13(kSelfBodyAffil2, "");                       // 0x491F87
                    g_Client.msg.System(Str(392));           // 0x491F9F (0x188)
                    // TODO(ui) [0x491FB4] UI_GuildCreate_Open (0x667DA0).
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(393));           // 0x491FCF
                } else if (p.statusCode == 2) {
                    g_Client.msg.System(Str(394));           // 0x491FF5
                }
                break;
            case 2: // guild tier upgrade — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x49200F
                if (p.statusCode == 0) {                     // 0x492029
                    // Sub-dispatch on dword_1675B10 (0x492050) — already modeled in
                    // Net/ClientState.h (set by SendPackets.cpp:2545 = request ctxId).
                    // The binary has NO `case 0`: value 0 (default) -> no effect,
                    // no stray message. Faithful without an extra guard.
                    switch (dword_1675B10) {
                        case 1: // 0x49207A: simply opens the manager
                            // TODO(state) [0x49207A] Crt_Memcpy(unk_1839970, blob, 0x56C).
                            // TODO(ui)    [0x492087] UI_GuildMgrWnd_Open(g_Guild) (0x667E20).
                            break;
                        case 2: { // 0x4920C0: actual tier upgrade
                            // TODO(state) [0x4920C0] Crt_Memcpy(unk_1839970, blob, 0x56C):
                            //   the blob overlaps state ALREADY modeled by g_Guild (50-member
                            //   roster) — storing it as a raw Blob would duplicate the state, so
                            //   not forced (cf. GuildSystem.h, layout partially deduced).
                            g_Guild.CountMembers();          // 0x4920CD (Guild_CountMembers)
                            // dword_1839980 = current GUILD TIER: absent from GuildRoster
                            // (Game/GuildSystem.h not owned by this front) and its original
                            // packet isn't identified in this module -> long-tail Var
                            // rather than a guessed field.
                            const int32_t tier = g_Client.VarGet(0x1839980);
                            if (g_Guild.memberCount >= 10 * tier) { // 0x4920E1
                                // {level, weight} thresholds per tier — 0x492136.
                                // NB: the global compared is g_InvWeight (0x16732AC), which the
                                // report called "gold"; we cite the address, not the label.
                                switch (tier) {
                                    case 1: // 0x492144 / 0x492176 (0x1312D00 = 20,000,000)
                                        if (g_World.self.level < 50)               g_Client.msg.System(Str(570)); // 0x492157
                                        else if (g_Client.inv.weight < 20000000)   g_Client.msg.System(Str(571)); // 0x492189
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5 (LABEL_42)
                                        break;
                                    case 2: // 0x4921AA / 0x4921DB
                                        if (g_World.self.level < 70)               g_Client.msg.System(Str(572)); // 0x4921BC
                                        else if (g_Client.inv.weight < 30000000)   g_Client.msg.System(Str(573)); // 0x4921EE
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5
                                        break;
                                    case 3: // 0x49220F / 0x492241
                                        if (g_World.self.level < 90)               g_Client.msg.System(Str(574)); // 0x492222
                                        else if (g_Client.inv.weight < 40000000)   g_Client.msg.System(Str(575)); // 0x492253
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5
                                        break;
                                    case 4: // 0x492274 / 0x4922A6
                                        if (g_World.self.level < 113)              g_Client.msg.System(Str(576)); // 0x492287
                                        else if (g_Client.inv.weight < 50000000)   g_Client.msg.System(Str(577)); // 0x4922B9
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5
                                        break;
                                    default:
                                        g_Client.msg.System(Str(569)); // 0x4922E0
                                        break;
                                }
                            } else {
                                g_Client.msg.System(Str(568)); // 0x4920F3 (insufficient members)
                            }
                            break;
                        }
                        case 3: // 0x4920A2: simple resync
                            // TODO(state) [0x4920A2] Crt_Memcpy(unk_1839970, blob, 0x56C) only.
                            break;
                        default: break; // no case 0 in the binary
                    }
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(391));           // 0x49231A
                }
                break;
            case 3: // 0x491EED (absent from the previous C++ switch)
                g_GmCmdCooldownLatch = 0;                    // 0x492334
                switch (p.statusCode) {                      // 0x49235A
                    case 0: g_Client.msg.System(Str(412));  break; // 0x492372
                    case 1: g_Client.msg.System(Str(413));  break; // 0x492398
                    case 2: g_Client.msg.System(Str(414));  break; // 0x4923BD
                    case 3: g_Client.msg.System(Str(2044)); break; // 0x4923E3
                    default: break;                                // silent
                }
                break;
            case 4: // leave/cancel — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x4923FD
                if (p.statusCode == 0) {                     // 0x492417
                    ResetGuildStateBlock();                  // 0x492442-0x4924F3
                    g_Client.msg.System(Str(478));           // 0x492508
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(479));           // 0x49252E
                } else if (p.statusCode == 2) {
                    g_Client.msg.System(Str(2046));          // 0x492554
                }
                break;
            case 5: // 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x49256E
                if (p.statusCode == 0) {                     // 0x492588
                    // TODO(state) [0x4925A2/0x4925B4/0x4925C6/0x4925D8] 4x Crt_StringInit on
                    //   string buffers not modeled (same targets as case 1).
                    g_Client.msg.System(Str(544));           // 0x4925F1
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(545));           // 0x492617
                }
                break;
            case 6: // dissolution — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x492631
                switch (p.statusCode) {                      // 0x492657
                    case 0:
                        ResetGuildStateBlock();              // 0x492668-0x492719
                        g_Client.msg.System(Str(470));       // 0x49272F
                        break;
                    case 1: g_Client.msg.System(Str(471));  break; // 0x492754
                    case 2: g_Client.msg.System(Str(468));  break; // 0x49277A
                    case 3: g_Client.msg.System(Str(2047)); break; // 0x4927A0
                    default: break;                                // silent
                }
                break;
            case 7: // invitation — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x4927BA
                if (p.statusCode == 0) {                     // 0x4927D4
                    g_Client.msg.System(Str(578));           // 0x4927F2
                    // TODO(ui) [0x492807] UI_GuildCreate_Open (0x667DA0).
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(579));           // 0x492822
                }
                break;
            case 8: // kick — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x49283C
                if (p.statusCode == 0) {                     // 0x492856
                    // TODO(state) [0x492874-0x492916] comparisons dword_1839991/183999E vs
                    //   byte_183A0F4 + 4x Crt_StringInit, and [0x4928EF]
                    //   dword_1839C38[10*dword_183A014 + dword_183A018] = 0: the selection
                    //   grid (183A014/018) and rank[] belong to GuildRoster
                    //   (Game/GuildSystem.h), declared OUT OF SCOPE — file not owned.
                    g_Guild.CountMembers();                  // 0x492923
                    g_Client.msg.System(Str(475));           // 0x492939
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(476));           // 0x49295F
                }
                break;
            case 9: // application / acceptance — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x492979
                if (p.statusCode == 0) {                     // 0x492993
                    // dword_183A0F0 = 1/2 selector (not modeled -> long-tail Var).
                    const int32_t sel = g_Client.VarGet(0x183A0F0);
                    if (sel == 1) {                          // 0x4929BA
                        // TODO(state) [0x4929D6] dword_1839C38[10*183A014+183A018] = 1 (rank,
                        //   out of scope, GuildSystem.h).
                        g_Client.msg.System(Str(550));       // 0x4929F1
                    } else if (sel == 2) {                   // 0x4929C3
                        // TODO(state) [0x492A15] same = 2.
                        g_Client.msg.System(Str(551));       // 0x492A31
                    }
                } else if (p.statusCode == 1) {              // 0x49299C
                    const int32_t sel = g_Client.VarGet(0x183A0F0); // 0x492A4B
                    if (sel == 1)      g_Client.msg.System(Str(552)); // 0x492A7B
                    else if (sel == 2) g_Client.msg.System(Str(553)); // 0x492AA1
                }
                break;
            case 10: // 0xA — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x492AC0
                // (1) REFUTATION of the gap report, which claimed "case 0xA writes NO
                //     state". FALSE — disasm 0x492AEE-0x492B0C:
                //       Crt_StringInit(dest = unk_1839D00 + 5*(10*dword_183A014 + dword_183A018),
                //                      src  = dword_183A101)
                //     It IS a write. TODO(state): unk_1839D00 = "5-byte block/member"
                //     of g_Guild (+920), explicitly OUT OF SCOPE in Game/GuildSystem.h
                //     (file not owned), indexed by the 183A014/018 grid, also out of
                //     scope. See (2): since the source is always empty, this is in practice
                //     a clear of the 5-byte block.
                // (2) dword_183A101 is PROVEN CONSTANTLY EMPTY, which fixes the messages without
                //     guessing: xrefs_to(0x183A101) = 3, and all 3 are READS, all
                //     in this same function (0x492AEE = src of the StringInit above,
                //     0x492B19 and 0x492B77 = arg1 of Crt_Strcmp) — NO write site anywhere in
                //     the whole binary; get_bytes(0x183A101) = 0x0 0x0 ... = empty string.
                //     Its neighbor byte_183A0F4 (written, itself, by UI_MsgBox_OnLButtonUp 0x5C0A90) can't
                //     overflow into it: 0x183A101 == 0x183A0F4 + 13, exactly the
                //     size of a NUL-terminated name (12 chars + NUL) — the two buffers are
                //     adjacent but disjoint.
                //     => Crt_Strcmp(dword_183A101, "") is ALWAYS 0 (the "empty" branch), so
                //        branches 557 (0x492B3B) and 559 (0x492B99) are DEAD CODE in the
                //        shipped binary, and only 558/560 are reachable. Reproduced as-is.
                if (p.statusCode == 0) {                     // 0x492ADA
                    g_Client.msg.System(Str(558));           // 0x492B5D (`strcmp == 0` branch)
                } else if (p.statusCode == 1) {              // 0x492AE3
                    g_Client.msg.System(Str(560));           // 0x492BBC (`strcmp == 0` branch)
                }
                break;
            case 12: // 0xC — toggle ON (absent from the previous C++ switch)
                g_GmCmdCooldownLatch = 0;                    // 0x492BD6
                if (p.statusCode == 0) {                     // 0x492BF0
                    // dword_16746C8: 4 xrefs = 2 writes here (0xC/0xD) + 2 REAL reads
                    //   (cGameHud_Render 0x64A9C4, cGameHud_OnMouseDown 0x62B6CD) — toggles a
                    //   HUD sprite variant unk_9465D0 + its hit-test. This sprite is not
                    //   modeled on the C++ side: setting the Var is necessary but not
                    //   sufficient for a visible effect (HUD wiring = a separate gap, to be tracked,
                    //   not to be forced here).
                    g_Client.Var(0x16746C8) = 1;             // 0x492BFD
                    // dword_1687278: kept for write fidelity, but WRITE-ONLY in the
                    //   binary (xrefs_to = 2, both these writes — no reader).
                    g_Client.Var(0x1687278) = 1;             // 0x492C07
                }
                break;
            case 13: // 0xD — toggle OFF, symmetric to 0xC (absent from the previous C++ switch)
                g_GmCmdCooldownLatch = 0;                    // 0x492C20
                if (p.statusCode == 0) {                     // 0x492C3A
                    g_Client.Var(0x16746C8) = 0;             // 0x492C47
                    g_Client.Var(0x1687278) = 0;             // 0x492C51
                }
                break;
            case 14: // 0xE — (absent from the previous C++ switch)
                g_GmCmdCooldownLatch = 0;                    // 0x492C6A
                switch (p.statusCode) {                      // 0x492C90
                    case 0: break;                                 // explicit no-op
                    case 1: case 5: g_Client.msg.System(Str(223));  break; // 0x492CAD
                    case 2:         g_Client.msg.System(Str(1717)); break; // 0x492CD3
                    case 4:         g_Client.msg.System(Str(1718)); break; // 0x492CF8
                    default: break;                                // silent
                }
                break;
            case 17: // 0x11 — voluntary departure / promotion
                g_GmCmdCooldownLatch = 0;                    // 0x492D12
                if (p.statusCode == 0) {                     // 0x492D2C
                    // TODO(state) [0x492D57-0x492DE5] 4x Crt_StringInit + [0x492DBE]
                    //   dword_1839C38[10*183A014+183A018] = 0, then [0x492DED] a scan of the 50
                    //   members `Crt_Strcmp(unk_18399AB + 13*i, byte_1673184)` and [0x492E2A]
                    //   dword_1839C38[i] = 2: rank[]/grid = OUT OF SCOPE (GuildSystem.h,
                    //   file not owned). GuildRoster::RemoveMember(name) already exists but
                    //   does NOT set rank=2 -> don't call it in its place (different semantics).
                    g_Client.Var(0x16746B8) = 2;             // 0x492E35
                    g_Client.msg.System(Str(2108));          // 0x492E50
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(2127));          // 0x492E73
                } else if (p.statusCode == 2) {
                    g_Client.msg.System(Str(2109));          // 0x492E95
                }
                break;
            default: // 0xB / 0xF / 0x10 and beyond: TOTAL no-op, latch NOT reset (faithful)
                break;
        }
    });

    // 0x54 GuildNoticeChat — guild/faction chat message: "<str543> [name] message".
    OnPacket<GuildNoticeChat>(sys, 0x54, [](const GuildNoticeChat& p) {
        const std::string nm   = Name(p.name);
        const std::string body = std::string(p.message, ::strnlen(p.message, sizeof p.message));
        g_Client.msg.Faction(Str(543) + " [" + nm + "] " + body, kChatColorFaction, nm.c_str());
    });

    // 0x56 TeamSlotAssign — writes a value into the current team slot (global cursor) then
    // continues the roster scan (Guild_SelectNextMember); a successful scan (next non-empty
    // slot found) sends Net_SendOp78(name[cursor]) — wired here (cf. resolved TODO(send) in
    // Game/GuildSystem.h: "to be wired from Net/GameHandlers_PartyGuild.cpp").
    OnPacket<TeamSlotAssign>(sys, 0x56, [&sys](const TeamSlotAssign& p) {
        const int idx = g_Client.VarGet(0x183A020);                     // dword_183A020 = cursor
        g_Client.Var(0x1839EDC + 4 * idx) = static_cast<int32_t>(p.value); // dword_1839EDC[idx]
        if (g_Guild.SelectNextMember()) {
            char name13[13] = {};
            const std::string& nm = g_Guild.members[g_Guild.cursor].name;
            const size_t n = nm.size() < sizeof name13 ? nm.size() : sizeof name13;
            std::memcpy(name13, nm.data(), n);
            Net_SendOp78(sys.Client(), name13);
        }
    });

    // 0x5c GuildActionResult — guild action result (3 cases; extra read but unused).
    OnPacket<GuildActionResult>(sys, 0x5c, [](const GuildActionResult& p) {
        (void)p.extra;
        switch (p.action) {
            case 1: if (p.flag == 0) g_Client.msg.System(Str(758)); break;
            case 2: if (p.flag == 0) g_Client.msg.System(Str(759)); break;
            case 3:
                if (p.flag == 0)      g_Client.msg.System(Str(760));
                else if (p.flag == 1) g_Client.msg.System(Str(761));
                break;
            default: break;
        }
    });

    // 0x5d PartyInviteResult — party invite result (5 cases; some with "[param]").
    OnPacket<PartyInviteResult>(sys, 0x5d, [](const PartyInviteResult& p) {
        const std::string param = std::to_string(p.param);
        switch (p.code) {
            case 1: g_Client.msg.System(Str(372)); break;
            case 2: g_Client.msg.System("[" + param + "]" + Str(373)); break; // color 3
            case 3: g_Client.msg.System(Str(374)); break;
            case 4: g_Client.msg.System("[" + param + "]" + Str(375)); break; // color 2
            case 5: g_Client.msg.System(Str(376)); break;
            default: break;
        }
    });

    // 0x7b PartyMemberTargetSet — sets a member's target (resolved by network identity).
    OnPacket<PartyMemberTargetSet>(sys, 0x7b, [](const PartyMemberTargetSet& p) {
        const int e = FindPlayerIndex(p.idHi, p.idLo);
        if (e >= 0) // word_1687454[454*e] = (u16)targetVal; entity stride = 908 bytes
            g_Client.Var(0x1687454 + 908 * e) =
                static_cast<int32_t>(static_cast<uint16_t>(p.targetVal));
    });

    // 0x7f PartyMemberHpSet — sets a member's current+max HP/MP (kind 1/2), messages if self.
    OnPacket<PartyMemberHpSet>(sys, 0x7f, [](const PartyMemberHpSet& p) {
        const int e = FindPlayerIndex(p.entityIdHi, p.entityIdLo);
        if (e >= 0 && (p.kind == 1 || p.kind == 2)) {
            g_Client.Var(0x1687458 + 908 * e) = p.curValue; // dword_1687458[227*e]
            g_Client.Var(0x168745C + 908 * e) = p.maxValue; // dword_168745C[227*e]
            if (e == 0) {                                   // self: message + action
                g_Client.msg.System(p.kind == 1 ? Str(2012) : Str(2013));
                // TODO(send): Player_QueueAction_op91 (EA 0x517490) queues an action onto
                //   g_PlayerCmdController (dword_1669170, large player-command struct — throttling/
                //   coalescing per tick) which eventually emits Net_SendOp91 (opcode 0x5B, no payload).
                //   DO NOT FORCE a direct Net_SendOp91 call here: g_PlayerCmdController isn't
                //   modeled in this module (cf. Net/CombatResultApply.cpp lines 108/196/240/321/347 and
                //   Game/MapWarp.cpp — same unresolved TODO(net), all pending this same player
                //   command queue system; precedent deliberately kept for consistency).
            }
        }
    });

    // 0x80 PartyMemberUpdate — updates a member field + handles departure (selector 4 = self).
    OnPacket<PartyMemberUpdate>(sys, 0x80, [](const PartyMemberUpdate& p) {
        const int e = FindPlayerIndex(p.idHi, p.idLo);
        if (e < 0) return;
        switch (p.selector) {
            case 1: case 2: case 3:
                g_Client.Var(0x1687458 + 908 * e) = static_cast<int32_t>(p.value1);
                g_Client.Var(0x168745C + 908 * e) = static_cast<int32_t>(p.value2);
                break;
            case 4:
                if (e == 0) { // self: leaves the party -> reset HP/MP + warp to town
                    g_Client.Var(0x1687458) = 0;
                    g_Client.Var(0x168745C) = 0;
                    g_Client.msg.System(Str(117));
                    // Resolves the warp target (guard + coords); the network send stays a
                    // TODO(send) internal to MapWarp.cpp (Net_SendPacket_Op20, EA 0x55c66f).
                    BeginWarpToFactionTown(static_cast<int32_t>(net::g_LocalElement), false, 0, &g_CoordResolver);
                }
                break;
            default: break;
        }
    });

    // 0x81 PartyItemResult — writes an inventory cell (status 1) or leaves the party (2).
    OnPacket<PartyItemResult>(sys, 0x81, [](const PartyItemResult& p) {
        if (p.status == 1) {
            g_Client.inv.Set(p.invRow, p.invCol, p.itemId,
                             p.gridPos % 8, p.gridPos / 8, 0, 0, 0);
            g_Client.msg.System(Str(1977));
        } else if (p.status == 2) {
            g_Client.msg.System(Str(117));
            g_Client.Var(0x1687458) = 0; // dword_1687458[0] (self)
            g_Client.Var(0x168745C) = 0; // dword_168745C[0]
            BeginWarpToFactionTown(static_cast<int32_t>(net::g_LocalElement));
        }
    });
}

} // namespace ts2::net

// Game/ChatCommands.cpp — see Game/ChatCommands.h for the module's contract.
//
// ============================================================================
// DEVIATION FROM THE INITIAL INSTRUCTION — read before anything else
// ============================================================================
// The mission asked to decompile Chat_SubmitTypedMessage (EA 0x5c3cf0) to derive
// the player prefixes (whisper/party/guild...), and suggested expanding to
// Chat_ParseGmCommand (0x68bfd0) if legitimate player prefixes turned out to be
// mixed in with the GM commands there.
//
// Decompiling 0x5c3cf0 (Chat_SubmitTypedMessage):
//   GetWindowTextA -> banned-word filter (maybe_Dict001_MatchWord 0x4c1410) ->
//   Net_SendOp80(&unk_846C08, String) (0x5c3d87).
// It is indeed confirmed by decompilation to be a near-trampoline: NO prefix
// detection, no channel routing. It sends the entire typed text as-is on
// opcode 80 (plain "say" chat). It is the input box used by g_hEditChatInput
// (0x1669000), distinct from the main chat box.
//
// Decompiling 0x68bfd0 (Chat_ParseGmCommand): 36 distinct commands, ALL GM/
// cheat commands (/movezone, /hide, /show, /exp, /money, /item, /moncall,
// /die, /max, /tribe, /equip, /unequip, /find, /call, /move, /nchat, /ychat,
// /kick, /block, /tribebank, /pvppoint, /level, /message, /editstr, /editdex,
// /editcon, /editint, /level2, /useitem, /delitem, /monkill, /movezonepos,
// /movepos, /pvpkill, /319Battle, /notice — /item and /nchat/ /ychat/kick/
// block each have 2-3 variants depending on the number of space-separated
// tokens in the line, but remain the same named command). NO legitimate
// player prefix (whisper/party/guild) is mixed in there — exhaustively
// confirmed by full decompilation (tokenizer: up to 5 space-separated tokens
// of 100 bytes max each, 0x68c043-0x68c129).
// Per the instruction, this logic remains out of scope and is NOT implemented
// here.
//
// The REAL player prefixes/channels (whisper/party/guild/alliance/trade/
// faction) live elsewhere: in UI_Chat_SubmitInput (EA 0x68b330), the function
// called by the MAIN chat box (g_hEditChatMain 0x1668FD4), which first tries
// Chat_ParseGmCommand (only if g_GmAuthLevel > 0, so never for a regular
// player) then, if it wasn't a GM command, routes based on:
//   - the line's first character if it is '!', '#', '@' or '~';
//   - otherwise the channel tab currently selected in the chat box.
// Contrary to the instruction's hypothesis ("/w ", "/p ", "/g "), TS2 does
// NOT use multi-character "/letter " prefixes for player channels: these are
// SINGLE-CHARACTER SYMBOLS glued to the text (no space after). The game's
// only textual "/xxx" prefixes are the GM commands of Chat_ParseGmCommand.
// So it is UI_Chat_SubmitInput, not Chat_SubmitTypedMessage, that is the
// relevant function for this mission; this file reproduces its PLAYER part.
// Cross-check: Docs/TS2_CLIENT_SHELL.md §2.5 (already documented by an earlier
// RE session) confirms exactly the same prefix/channel -> opcode mapping as
// the decompilation below.
// ============================================================================

#include "Game/ChatCommands.h"

namespace ts2::game {

namespace {

// Builds a command from the text following a special prefix. Reproduces the
// `if (v36[0])` test present before every '!'/'#'/'@'/'~' branch in
// UI_Chat_SubmitInput (0x68b54e, 0x68b677, 0x68b7a0, 0x68b8ff): if nothing
// follows the prefix character, the original client sends NOTHING (no error,
// no packet).
ChatCommand MakePrefixed(ChatCommandKind kind, const std::string& rest) {
    ChatCommand cmd;
    if (rest.empty()) {
        return cmd; // kind stays None -> the caller must send nothing
    }
    cmd.kind = kind;
    cmd.message = rest;
    return cmd;
}

} // namespace

ChatCommand ParseChatInput(const std::string& raw, ChatChannelMode currentChannelMode) {
    ChatCommand cmd;

    // GetWindowTextA returns 0 on an empty box -> the original client exits
    // without doing anything (0x68b370/0x68bf2a). Same behavior here: kind
    // stays None.
    if (raw.empty()) {
        return cmd;
    }

    // NOTE (out of scope, not reproduced here): before the prefix switch, the
    // client compares the ENTIRE text against 3 localized strings via
    // StrTable005_Get (indices 738/739/740, loaded from 005.DAT — not
    // statically available) to trigger a warehouse-opening shortcut
    // (Net_SendVaultReq_234(1|2|3), EA 0x68b3c5-0x68b4b0). This is not a chat
    // command in the sense of this mission (whisper/party/guild): not
    // implemented, left as a TODO for a future module if it ever needs
    // reproducing.

    // switch(String[0]) — EA 0x68b531/0x68b53e. The prefix character is NOT
    // followed by a space in the original binary (contrary to the instruction's
    // "/w " hypothesis): the rest of the buffer (from index 1) is used as-is as
    // the message body.
    const char first = raw[0];
    const std::string rest = raw.substr(1);

    // The FOUR prefixes additionally share a common guard not reproduced here:
    // Map_IsArenaZone() (0x54b690) always blocks the channel (message
    // StrTable005[1352], LABEL_86 shared at 0x68be5a); '#' and '@' additionally
    // add g_SelfMorphNpcId == 291 to the same test (0x68b945/0x68b7e5). See
    // also the generic TODO at the top of this file.
    switch (first) {
        case '!':
            // Guild: Net_SendOp77, EA 0x68b733. Guards not reproduced here: arena
            // (Map_IsArenaZone, 0x68b683) then Crt_Strcmp(unk_16746A8, raw)
            // (0x68b6bc) -> error message StrTable005[371] if equal (exact role
            // of unk_16746A8 not statically confirmed — TODO).
            return MakePrefixed(ChatCommandKind::Guild, rest);
        case '#':
            // Faction: Net_SendOp40, EA 0x68ba50. Guards not reproduced: arena or
            // g_SelfMorphNpcId==291 (0x68b945); reserved for players whose
            // g_SelfMorphNpcId is in {37,119,124,170,50,52} (0x68b9a4-0x68b9d6),
            // with a second restriction by g_LocalElement (0x68ba02-0x68ba3d).
            return MakePrefixed(ChatCommandKind::Faction, rest);
        case '@':
            // Trade: Net_SendOp81, EA 0x68b894. Guards not reproduced: arena or
            // g_SelfMorphNpcId==291 (0x68b7e5); blocked if g_SelfMorphNpcId ==
            // table{138,139,165,166}[g_LocalElement] (0x68b846-0x68b881).
            return MakePrefixed(ChatCommandKind::Trade, rest);
        case '~':
            // Alliance: Net_SendOp68, EA 0x68b60a. Guards not reproduced: arena
            // (0x68b55a) then Crt_Strcmp(g_AllianceRosterNames, raw) (0x68b593) ->
            // StrTable005[355] if equal.
            return MakePrefixed(ChatCommandKind::Alliance, rest);
        default:
            break;
    }

    // No special prefix -> routes based on the active channel tab
    // (switch(*(this + 161)), EA 0x68bb10). The text is sent in full (no
    // character stripped: the builders are called with &String in full,
    // e.g. 0x68bba9, 0x68bc22).
    cmd.message = raw;
    switch (currentChannelMode) {
        case ChatChannelMode::Whisper:
            // case 0, EA 0x68bb1c-0x68bba9: Net_SendOp39. Guards not reproduced:
            // arena (Map_IsArenaZone, 0x68bb1c) then Crt_Stricmp(target,
            // byte_1673184) (0x68bb5d) -> StrTable005[303] if the target is
            // oneself. The target itself (this+162) is NOT in `raw`: see
            // ChatCommand::target in the .h. The caller must fill it in.
            cmd.kind = ChatCommandKind::Whisper;
            break;
        case ChatChannelMode::Party:
            // case 1, EA 0x68bb10-0x68bc22: Net_SendOp38. No arena guard here
            // (the only player channel not blocked in an arena zone): blocked
            // only if g_SelfMorphNpcId == table{138,139,165,166}[g_LocalElement]
            // (0x68bbb3-0x68bbed, StrTable005[2040] if blocked).
            cmd.kind = ChatCommandKind::Party;
            break;
        case ChatChannelMode::Alliance:
            // case 2, EA 0x68bc31-0x68bca9: Net_SendOp68. Guards not reproduced:
            // arena (0x68bc31) then the same g_AllianceRosterNames sentinel guard
            // as the '~' prefix (0x68bc74).
            cmd.kind = ChatCommandKind::Alliance;
            break;
        case ChatChannelMode::Guild:
            // case 3, EA 0x68bcb8-0x68bd30: Net_SendOp77. Guards not reproduced:
            // arena (0x68bcb8) then the same unk_16746A8 sentinel guard as the
            // '!' prefix (0x68bcfb).
            cmd.kind = ChatCommandKind::Guild;
            break;
        case ChatChannelMode::Trade:
            // case 4, EA 0x68bd78-0x68be10: Net_SendOp81. Same guard as the '@'
            // prefix: arena or g_SelfMorphNpcId==291 (0x68bd78), then
            // table{138,139,165,166}[g_LocalElement] (0x68bda1-0x68bddb,
            // StrTable005[2041]).
            cmd.kind = ChatCommandKind::Trade;
            break;
        case ChatChannelMode::Faction:
            // case 5, EA 0x68be58-0x68bf25: Net_SendOp40. Same guard as the '#'
            // prefix: arena or g_SelfMorphNpcId==291 (0x68be58), then a morph
            // whitelist + table by g_LocalElement.
            cmd.kind = ChatCommandKind::Faction;
            break;
    }

    return cmd;
}

} // namespace ts2::game

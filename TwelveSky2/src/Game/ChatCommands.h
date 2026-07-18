// Game/ChatCommands.h — Prefix/channel detection for the main chat box.
//
// Source of truth: decompilation of UI_Chat_SubmitInput (EA 0x68b330), the REAL
// PLAYER chat command routing function (whisper/party/guild/alliance/
// trade/faction). See the .cpp for the details of the deviation from the
// mission's initial instruction (which targeted Chat_SubmitTypedMessage
// 0x5c3cf0 and suggested expanding to Chat_ParseGmCommand 0x68bfd0 if needed).
//
// This module does NO network sending: ParseChatInput() only faithfully
// reproduces prefix/channel detection and text/target extraction; it is up
// to the caller to map ChatCommand::kind to the right Net_SendOpNN builder
// in Net/SendPackets.h (already written, not modified here) and to apply the
// gameplay guards (arena zone, morph restrictions, etc. — see TODO in the
// .cpp).
#pragma once

#include <string>

namespace ts2::game {

// Chat channel resulting from parsing a raw chat line.
// Maps 1:1 to the branches of UI_Chat_SubmitInput (0x68b330) and the outbound
// opcodes they call (Net/SendPackets.h, already written):
//   Whisper  -> Net_SendOp39  (0x4b75d0) — 13B name + 61B message
//   Party    -> Net_SendOp38  (0x4b7450) — 61B message ("groupe" in Docs/TS2_CLIENT_SHELL.md §2.5)
//   Alliance -> Net_SendOp68  (0x4ba100) — 61B message
//   Guild    -> Net_SendOp77  (0x4bae60) — 61B message
//   Trade    -> Net_SendOp81  (0x4bb470) — 61B message ("commerce" in the doc)
//   Faction  -> Net_SendOp40  (0x4b7760) — 61B message
// Not to be confused with the plain "say" channel (Op80, Net_SendChatNormal_Op80 /
// Net_SendOp80 0x4bb2f0), which is a totally separate path — see
// Chat_SubmitTypedMessage (0x5c3cf0) and the comment at the top of the .cpp.
enum class ChatCommandKind {
    None,      // Empty line, or special prefix followed by an empty body (nothing to
               // send, cf. the `if (v36[0])` tests before any processing:
               // 0x68b54e/0x68b677/0x68b7a0/0x68b8ff)
    Whisper,
    Party,
    Alliance,
    Guild,
    Trade,
    Faction,
};

// Active channel tab in the chat box when the typed line does not start with
// ANY special prefix. Mirrors the `switch (*(this + 161))` of
// UI_Chat_SubmitInput (0x68bb10), same numeric values as observed in the
// disassembly and documented in Docs/TS2_CLIENT_SHELL.md §2.5 (channel mode
// field of g_ChatManager, +0x284/644 dec). ParseChatInput() does not read
// this state itself (it lives in the original UI object, out of scope for
// this module): the caller supplies it explicitly.
enum class ChatChannelMode : int {
    Whisper  = 0,
    Party    = 1,
    Alliance = 2,
    Guild    = 3,
    Trade    = 4,
    Faction  = 5,
};

// Result of parsing a raw chat line.
struct ChatCommand {
    ChatCommandKind kind = ChatCommandKind::None;

    // Recipient name. ONLY relevant for kind == Whisper, and does NOT come
    // from the typed text: in the original client, the whisper target is a
    // separate field (this+162, 13 bytes) set by UI_Chat_SetWhisperMode
    // (0x68b260, e.g. right-click on a player), distinct from the edited
    // line. ParseChatInput() therefore cannot reconstruct it from `raw`
    // alone: this field stays empty and it is up to the caller to fill it in
    // from its own whisper-mode state before calling Net_SendOp39.
    std::string target;

    // Message text, channel prefix stripped if applicable.
    std::string message;
};

// Faithfully reproduces the PLAYER-side prefix/channel detection of
// UI_Chat_SubmitInput (0x68b330): first special-prefix character
// ('!'=Guild, '#'=Faction, '@'=Trade, '~'=Alliance), otherwise
// `currentChannelMode` (active tab, cf. ChatChannelMode above). Performs
// neither network sending nor profanity filtering (maybe_Dict001_MatchWord
// 0x4c1410, 001.DAT data not statically available) nor gameplay guards
// (arena/morph — see TODO in the .cpp): all of that remains the caller's
// responsibility.
//
// raw               : raw content of the chat EDIT box (equivalent to the
//                      `String` buffer filled by GetWindowTextA at 0x68b368).
// currentChannelMode : active tab if `raw` starts with no special prefix.
ChatCommand ParseChatInput(const std::string& raw, ChatChannelMode currentChannelMode);

} // namespace ts2::game

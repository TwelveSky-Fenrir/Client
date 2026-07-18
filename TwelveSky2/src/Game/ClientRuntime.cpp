// Game/ClientRuntime.cpp — implementation of the client runtime state hub.
#include "Game/ClientRuntime.h"
#include "Game/StringTables.h"   // g_Strings.messages (005.DAT, mMESSAGE): real Str(id) text; g_Strings.colors: whisper color
#include <cstdio>
#include <cstring>

namespace ts2::game {

void MessageLog::Push(MessageLine&& l) {
    lines_.push_back(std::move(l));
    while (lines_.size() > kMaxLines) lines_.pop_front();
}

void MessageLog::System(const std::string& t, uint32_t color) {
    Push(MessageLine{ t, color, MsgKind::System, 0, {} });
}
void MessageLog::Chat(const std::string& t, uint32_t color, const char* who) {
    Push(MessageLine{ t, color, MsgKind::Chat, 0, who ? who : "" });
}
void MessageLog::Whisper(const std::string& t, const char* who) {
    // Pkt_WhisperReceive 0x48F210: the color is a palette INDEX into mFONTCOLOR
    // (ColorTable_InitPalette 0x4C1D60). Proven rule (@0x48f2bd): if the sender
    // NAME's first 4 bytes equal "[GM]" -> GM color (g_ChatColor_GM 0x84DFF4 =
    // idx45, @0x48f2e3); otherwise whisper color (g_ChatColor_Whisper 0x84DFDC =
    // idx1, @0x48f304). The binary stores the index and resolves it at draw time
    // (ColorTable_GetColor 0x4C1FE0); resolved here at the producer instead.
    // (Replaces the made-up value 0xFFFF80FF; whisper idx1 = 0xFFFFFFFF, GM idx45 = 0xFFFF8040.)
    const bool isGm = who && std::strncmp(who, "[GM]", 4) == 0; // @0x48f2bd (Crt_Strcmp 4 bytes)
    const uint32_t color = isGm ? g_Strings.colors.GmColor()       // @0x48f2e3
                                : g_Strings.colors.WhisperColor();  // @0x48f304
    Push(MessageLine{ t, color, MsgKind::Whisper, 0, who ? who : "" });
}
void MessageLog::Faction(const std::string& t, uint32_t color, const char* who) {
    Push(MessageLine{ t, color, MsgKind::Faction, 0, who ? who : "" });
}
void MessageLog::Floating(int floatType, int /*flag*/, const std::string& t, uint32_t color) {
    Push(MessageLine{ t, color, MsgKind::Floating, floatType, {} });
}

std::string Str(int id) {
    // StrTable005 (005.DAT, mMESSAGE), actually loaded (App::Init): REAL
    // localized text, 1-based index, "" out of bounds (faithful to
    // StrTable005_Get 0x4C1D20). Falls back to the "#id" placeholder while
    // the table hasn't been loaded yet (e.g. isolated unit tests, incomplete
    // App_Init sequence) — distinguishes "not loaded yet" (zero count) from
    // "id out of bounds" (nonzero count but invalid index, which legitimately
    // returns "").
    if (g_Strings.messages.Count() != 0)
        return std::string(g_Strings.messages.Get(id));

    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%d", id);
    return std::string(buf);
}

} // namespace ts2::game

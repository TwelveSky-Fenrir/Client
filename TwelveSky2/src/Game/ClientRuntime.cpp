// Game/ClientRuntime.cpp — implémentation du hub d'état runtime client.
#include "Game/ClientRuntime.h"
#include "Game/StringTables.h"   // g_Strings.messages (005.DAT, mMESSAGE) : vrai texte de Str(id)
#include <cstdio>

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
    Push(MessageLine{ t, 0xFFFF80FFu, MsgKind::Whisper, 0, who ? who : "" });
}
void MessageLog::Faction(const std::string& t, uint32_t color, const char* who) {
    Push(MessageLine{ t, color, MsgKind::Faction, 0, who ? who : "" });
}
void MessageLog::Floating(int floatType, int /*flag*/, const std::string& t, uint32_t color) {
    Push(MessageLine{ t, color, MsgKind::Floating, floatType, {} });
}

std::string Str(int id) {
    // StrTable005 (005.DAT, mMESSAGE) réellement chargée (App::Init) : texte
    // localisé RÉEL, index 1-based, "" hors bornes (fidèle à StrTable005_Get
    // 0x4C1D20). Repli sur le placeholder "#id" tant que la table n'a pas encore
    // été chargée (ex. tests unitaires isolés, séquence App_Init incomplète) —
    // distingue « pas encore chargé » (compteur nul) de « id hors bornes »
    // (compteur non nul mais index invalide, qui renvoie légitimement "").
    if (g_Strings.messages.Count() != 0)
        return std::string(g_Strings.messages.Get(id));

    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%d", id);
    return std::string(buf);
}

} // namespace ts2::game

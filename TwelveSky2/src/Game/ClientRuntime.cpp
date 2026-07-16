// Game/ClientRuntime.cpp — implémentation du hub d'état runtime client.
#include "Game/ClientRuntime.h"
#include "Game/StringTables.h"   // g_Strings.messages (005.DAT, mMESSAGE) : vrai texte de Str(id) ; g_Strings.colors : couleur chuchotement
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
    // Pkt_WhisperReceive 0x48F210 : la couleur est un INDEX de canal de la palette
    // mFONTCOLOR (ColorTable_InitPalette 0x4C1D60). Regle prouvee (@0x48f2bd) : si les
    // 4 premiers octets du NOM de l'expediteur valent "[GM]" -> couleur GM
    // (g_ChatColor_GM 0x84DFF4 = idx45, @0x48f2e3) ; sinon couleur chuchotement
    // (g_ChatColor_Whisper 0x84DFDC = idx1, @0x48f304). Le binaire stocke l'index et le
    // resout au dessin (ColorTable_GetColor 0x4C1FE0) ; on resout ici chez le producteur.
    // (Remplace la valeur inventee 0xFFFF80FF ; whisper idx1 = 0xFFFFFFFF, GM idx45 = 0xFFFF8040.)
    const bool isGm = who && std::strncmp(who, "[GM]", 4) == 0; // @0x48f2bd (Crt_Strcmp 4 octets)
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

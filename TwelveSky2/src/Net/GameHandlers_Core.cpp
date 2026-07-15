// Net/GameHandlers_Core.cpp — branche les méga-dispatchers reversés DIRECTEMENT
// depuis IDA (workflow ts2-ida-gameplay-core) sur le réseau. Ces implémentations
// sont plus fidèles/complètes que les versions simplifiées posées par les modules
// de domaine (GameHandlers_Misc pour 0x16, GameHandlers_Entity pour 0x11/0x15) :
// ce module est donc enregistré EN DERNIER dans InstallGameHandlers pour les
// remplacer (le dispatcher n'a qu'un seul slot par opcode — le dernier gagne).
//
//   0x11 CharStatDelta       -> game::ApplyCharStatDelta       (32 sous-cas, EA 0x465d90)
//   0x15 OnCombatResult      -> game::ApplyCombatResult        (bloc 76o,   EA 0x55a380)
//   0x16 SetGameVar          -> game::ApplySetGameVar          (133 cas,    EA 0x468370)
//   0x1a ItemActionDispatch  -> game::ApplyItemActionDispatch  (trou comblé, EA 0x46a320)
//   0x5e WorldEntityDispatch -> game::ApplyWorldEntityDispatch (trou comblé PARTIEL,
//        sous-opcodes 1..18/~300, EA 0x494870 — cf. Net/WorldEntityDispatch.h pour la carte
//        et les TODO du reste du mega-switch)
//   0x27 QuestInteractResult -> complète (state) le handler existant de GameHandlers_Misc
//        (messages haut-niveau déjà posés là ; on y ajoute ApplyQuestInteractResultState :
//        écriture inventaire + compteurs de quête + messages de boucle de récompense).
//
// Les payloads bruts correspondent octet à octet aux layouts attendus par ces
// fonctions (mêmes offsets que les structs RecvPackets équivalentes) : on les
// leur passe directement, sans repasser par un Parse() intermédiaire.
#include "Net/GameHandlers.h"
#include "Net/GameVarDispatch.h"
#include "Net/CharStatDeltaDispatch.h"
#include "Net/CombatResultApply.h"
#include "Net/ItemActionDispatch.h"
#include "Net/WorldEntityDispatch.h"
#include "Game/QuestSystem.h"
#include "Game/ClientRuntime.h"

namespace ts2::net {

void RegisterCoreOverrideHandlers(NetSystem& sys) {
    sys.On(0x11, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyCharStatDelta(payload, len);
    });
    sys.On(0x15, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyCombatResult(payload, len);
    });
    sys.On(0x16, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplySetGameVar(payload, len);
    });
    sys.On(0x1a, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyItemActionDispatch(payload, len);
    });
    sys.On(0x5e, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyWorldEntityDispatch(payload, len);
    });

    // 0x27 QuestInteractResult : réplique les messages haut-niveau de
    // GameHandlers_Misc (pour ne pas dépendre de l'ordre d'enregistrement) puis
    // applique l'état réel (inventaire + compteurs + messages de récompense).
    OnPacket<QuestInteractResult>(sys, 0x27, [](const QuestInteractResult& p) {
        using namespace game;
        switch (p.resultCode) {
        case 1: g_Client.msg.System(Str(109)); break;
        case 2: g_Client.msg.System(Str(432)); break;
        case 3: g_Client.msg.System(Str(433)); break;
        case 4: g_Client.msg.System(Str(434)); break;
        case 5: g_Client.msg.System(Str(435)); break;
        case 6: g_Client.msg.System(Str(436)); break;
        case 7: g_Client.msg.System(Str(438)); break;
        case 8: g_Client.msg.System(Str(437)); break;
        case 9: g_Client.msg.System(Str(439)); break;
        default: break;
        }
        game::QuestInteractResultPacket pkt;
        pkt.resultCode = p.resultCode;
        pkt.invRow     = static_cast<int32_t>(p.invRow);
        pkt.invSlot     = p.invSlot;
        pkt.gridX       = p.gridX;
        pkt.gridY       = p.gridY;
        game::ApplyQuestInteractResultState(pkt, g_QuestProgress, g_Client.inv);
    });
}

} // namespace ts2::net

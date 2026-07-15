// Game/EnterWorldFlow.cpp — voir EnterWorldFlow.h pour le flux découvert et les EAs.
#include "Game/EnterWorldFlow.h"

namespace ts2::game {

namespace {
constexpr int kWaitBeforeUnloadFrames = 30;   // 0x1E @0x52C02C
constexpr int kZoneResourceStepFrames = 10;   // 0xA  @0x52C0DC
constexpr int kZoneResourceCount      = 20;   // @0x52C11C
constexpr int kSendRequestWaitFrames  = 30;   // 0x1E @0x52C15C
constexpr int kServerAckTimeoutFrames = 5000; // 0x1388 @0x52C203

constexpr int kStrIdSendFailed  = 67; // @0x52C1A2
constexpr int kStrIdAckTimeout  = 68; // @0x52C213

// Un hook nul est traité comme un no-op silencieux (sûr pour des tests unitaires
// partiels du flux sans brancher tous les callbacks).
inline void Call(const std::function<void()>& f) { if (f) f(); }
} // namespace

bool EnterWorldFlow_Update(EnterWorldFlowState& s, const EnterWorldFlowHost& host, int zoneId) {
    switch (s.state) {

    case EnterWorldState::WaitBeforeUnload: // case 0 @0x52BFF9
        ++s.frameCounter;
        if (s.frameCounter >= kWaitBeforeUnloadFrames) {
            Call(host.ResetUiAndAudio);
            s.zoneResourceIndex = 0;
            s.previousZoneId    = zoneId - 1; // this+15727 = dword_1675A9C - 1 @0x52C0A7
            s.state         = EnterWorldState::LoadZoneResources;
            s.frameCounter  = 0;
        }
        return true;

    case EnterWorldState::LoadZoneResources: // case 1 @0x52C0CF
        ++s.frameCounter;
        if (s.frameCounter >= kZoneResourceStepFrames) {
            if (host.LoadZoneResource) {
                host.LoadZoneResource(zoneId, s.zoneResourceIndex); // @0x52C0F8
            }
            ++s.zoneResourceIndex; // @0x52C11C
            if (s.zoneResourceIndex >= kZoneResourceCount) {
                s.state = EnterWorldState::SendEnterRequest; // @0x52C130
            }
            s.frameCounter = 0; // reset inconditionnel à chaque étape de 10 frames, @0x52C121
        }
        return true;

    case EnterWorldState::SendEnterRequest: { // case 2 @0x52C14C
        ++s.frameCounter;
        if (s.frameCounter >= kSendRequestWaitFrames) {
            // NOTE : l'original écrase ici g_SelfMorphNpcId par la zone cible (après en
            // avoir sauvegardé l'ancienne valeur dans dword_1675A94, jamais relue dans
            // cette fonction). Appartient au système de morph/téléport — délibérément
            // PAS reproduit ici (hors périmètre "flux de scène pur"), cf. TODO du header.
            bool sent = host.SendEnterWorldRequest && host.SendEnterWorldRequest(); // @0x52C18D
            if (sent) {
                s.state = EnterWorldState::WaitServerAck; // @0x52C1D7
            } else {
                if (host.ShowErrorNotice) host.ShowErrorNotice(kStrIdSendFailed); // @0x52C1AF
                s.state = EnterWorldState::Failed; // @0x52C1B7
            }
            s.frameCounter = 0;
        }
        return true;
    }

    case EnterWorldState::WaitServerAck: // case 3 @0x52C1F6
        ++s.frameCounter;
        if (s.frameCounter >= kServerAckTimeoutFrames) {
            if (host.ShowErrorNotice) host.ShowErrorNotice(kStrIdAckTimeout); // @0x52C220
            s.state        = EnterWorldState::Failed; // @0x52C228
            s.frameCounter = 0;
        }
        // Sortie normale (succès) NON observable ici : voir commentaire du header —
        // pilotée depuis l'extérieur par Pkt_EnterWorld (réseau, opcode 12 entrant).
        return true;

    case EnterWorldState::Failed: // default @0x52C232
    default:
        return false;
    }
}

} // namespace ts2::game

// Net/CharSelectPackets.cpp — implémentation. Voir CharSelectPackets.h pour le
// niveau de confiance RE (RECONFIRMÉ par décompilation fraîche via idaTs2, 2026-07-14).
//
// NB : les 5 fonctions d'origine (0x5298F0/0x52A4A0/0x52A740/0x52B070/0x52B310)
// construisent CHACUNE leur propre trame inline dans le binaire (nonces + en-tête 9o +
// XOR + send, motif dupliqué 5 fois à l'octet près). On regroupe fidèlement ce motif
// commun dans SendFrame() ci-dessous plutôt que de le dupliquer 5 fois en C++ — les
// octets produits sur le fil sont identiques, seule l'organisation du code source
// diffère du binaire (qui n'a aucune fonction commune à cet endroit faute d'inlining
// du compilateur d'origine).
#include "Net/CharSelectPackets.h"
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace ts2::net {

namespace {

// Réception bloquante d'exactement `need` octets dans nc.recvBuf (à partir de
// l'offset 0). Même motif que Net/Login.cpp::RecvExact (fichier distinct, pas
// exporté) — la socket est encore SYNCHRONE ici (WSAAsyncSelect pas encore posé,
// cf. Net_ConnectGameServer, qui n'a pas été appelé à ce stade du flux).
bool RecvExact(NetClient& nc, int need) {
    for (int i = 0; i != need; ) {
        int n = recv(nc.sock, nc.recvBuf + i, kRecvBufSize - i, 0);
        if (n <= 0) return false;
        i += n;
    }
    return true;
}

// Copie une chaîne dans un champ de taille fixe `n`, zéro-rempli (tronque si trop
// long). Reproduit le motif CopyField128 de Net/Login.cpp pour un champ plus court
// (nom de personnage, 13 octets à l'offset 20 de la fiche — cf. header).
void CopyFieldN(uint8_t* dst, size_t n, const std::string& src) {
    std::memset(dst, 0, n);
    const size_t c = std::min(n, src.size());
    std::memcpy(dst, src.data(), c);
}

// Lit un code résultat i32 à recvBuf[1] (non chiffré), même convention que
// Net_LoginRequest/Net_ConnectGameServer (Net/Login.cpp) : 1 octet de tête (écho
// d'opcode/statut bas niveau, non exploité ici) puis le code sur 4 octets LE.
int32_t ReadResultCode(NetClient& nc) {
    int32_t code = 0;
    std::memcpy(&code, nc.recvBuf + 1, 4);
    return code;
}

// Rng_Next() % 10000 x2 pour chaque nonce — même algorithme/même ordre de
// consommation que Net/Login.cpp::MakeNonces (dupliqué ici, fonction locale non
// exportée là-bas), confirmé identique dans les 5 fonctions CharSelect par
// décompilation fraîche.
inline int Rng() { return std::rand(); }
inline void MakeNonces(uint32_t& nonce1, uint32_t& nonce2) {
    int a = Rng() % 10000;
    int b = Rng() % 10000;
    nonce1 = static_cast<uint32_t>(a * b);
    int c = Rng() % 10000;
    int d = Rng() % 10000;
    nonce2 = static_cast<uint32_t>(c * d);
}

// Envoi bloquant intégral. Même motif que Net/Login.cpp::SendAll.
bool SendAllBlocking(SOCKET s, const uint8_t* buf, int len) {
    int off = 0;
    while (len > 0) {
        int n = send(s, reinterpret_cast<const char*>(buf) + off, len, 0);
        if (n == SOCKET_ERROR) return false;
        len -= n;
        off += n;
    }
    return true;
}

// Construit l'en-tête 9o [nonce1:4][nonce2_lo:3][seq:1@7][opcode:1@8] + `payload`,
// chiffre XOR l'ensemble avec nc.xorKey, envoie en bloquant. Sur succès : incrémente
// nc.seq (motif ++byte_8156A5 des 5 fonctions d'origine) et renvoie true. Sur échec :
// ferme la socket (Net_CloseSocket) et renvoie false — l'appelant traduit en
// kCharSelectErrSend (101), comme le binaire.
bool SendFrame(NetClient& nc, uint8_t opcode, const void* payload, size_t payloadLen) {
    std::vector<uint8_t> pkt(9 + payloadLen);
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);
    std::memcpy(pkt.data() + 0, &nonce1, 4);
    std::memcpy(pkt.data() + 4, &nonce2, 4); // octet [7] écrasé juste après par seq
    pkt[7] = nc.seq;
    pkt[8] = opcode;
    if (payloadLen)
        std::memcpy(pkt.data() + 9, payload, payloadLen);
    for (auto& b : pkt)
        b ^= nc.xorKey;

    if (!SendAllBlocking(nc.sock, pkt.data(), static_cast<int>(pkt.size()))) {
        NetCloseSocket(nc);
        return false;
    }
    ++nc.seq;
    return true;
}

} // namespace

void ParseCharRecord(const uint8_t* rec, game::CharSlotInfo& out) {
    out = game::CharSlotInfo{};

    // Nom : C-string, max 13 o (dont NUL) — cf. kCharRecFieldName. On copie dans un
    // tampon local NUL-terminé plutôt que de suivre un memchr sur `rec` directement,
    // pour ne jamais lire au-delà des 13 o même si le champ n'est pas terminé par un
    // NUL dans une fiche corrompue/partielle.
    char nameBuf[14] = {}; // 13 o de champ + 1 NUL de garde
    std::memcpy(nameBuf, rec + kCharRecFieldName, 13);
    nameBuf[13] = '\0';
    out.name = nameBuf;

    // Occupation : Crt_Strcmp(name, "") != 0, EA 0x51c2f7 — reproduit par name non vide
    // (std::string s'arrête déjà au premier NUL interne, comme Crt_Strcmp).
    out.occupied = !out.name.empty();
    if (!out.occupied) return; // fidèle : le binaire n'exploite aucun autre champ d'une fiche vide

    std::memcpy(&out.job,        rec + kCharRecFieldJob,     4);
    std::memcpy(&out.faction,    rec + kCharRecFieldFaction, 4);
    std::memcpy(&out.face,       rec + kCharRecFieldFace,    4);
    std::memcpy(&out.hairColor, rec + kCharRecFieldHair,    4);
    std::memcpy(&out.power,      rec + kCharRecFieldPower,   4);
    std::memcpy(&out.localZoneId, rec + kCharRecFieldZoneId, 4);

    // Position locale : stockée int32 dans le binaire, castée en float À L'USAGE
    // (flt_1675AAC = (float)dword_166A8E0[...], EA 0x51c79e et suivants) — reproduit
    // tel quel ici (lecture int32 puis conversion), pas une lecture float directe.
    int32_t rawX = 0, rawY = 0, rawZ = 0;
    std::memcpy(&rawX, rec + kCharRecFieldPosX, 4);
    std::memcpy(&rawY, rec + kCharRecFieldPosY, 4);
    std::memcpy(&rawZ, rec + kCharRecFieldPosZ, 4);
    out.localPosX = static_cast<float>(rawX);
    out.localPosY = static_cast<float>(rawY);
    out.localPosZ = static_cast<float>(rawZ);
}

void LoadCharacterSlotsFromRecords(std::array<game::CharSlotInfo, game::kMaxCharSlots>& slots) {
    static_assert(game::kMaxCharSlots == kCharRecordCount,
                  "kMaxCharSlots doit rester synchronise avec kCharRecordCount (NetClient.h)");
    for (int i = 0; i < game::kMaxCharSlots; ++i)
        ParseCharRecord(g_CharRecords[static_cast<size_t>(i)], slots[static_cast<size_t>(i)]);
}

int32_t AccountKeepAlive(NetClient& nc) {
    // Opcode 12, SANS payload (9 o, en-tête seul) — confirmé par décompilation fraîche
    // de 0x5298F0. PAS d'attente de réponse (heartbeat fire-and-forget confirmé) : le
    // binaire fait *a1=0 immédiatement après un envoi réussi, sans recv().
    if (!SendFrame(nc, 12, nullptr, 0)) return kCharSelectErrSend;
    return 0;
}

int32_t CreateCharacter(NetClient& nc, int32_t slot, const game::CharCreateForm& form,
                        int32_t lookPresetId) {
    // Opcode 17. Payload RÉEL 10092 o = 4 o slot + 10088 o fiche personnage (confirmé
    // par décompilation fraîche de 0x52A4A0 + de l'appelant Scene_CharSelectOnMouseUp
    // EA 0x526634-0x5267E4). La fiche est quasi entièrement zéro ; seuls les offsets
    // suivants sont écrits avant l'envoi dans TOUS les chemins observés du binaire :
    //   [20..32] nom (13 o) · [36] job · [44] faction · [48] face · [52] hairColor ·
    //   [216] lookPresetId (résolu job×variant, cf. CharSelectFlow::ResolveLookPresetId).
    std::vector<uint8_t> payload(4 + 10088, 0);
    std::memcpy(payload.data(), &slot, 4);
    uint8_t* rec = payload.data() + 4;
    CopyFieldN(rec + 20, 13, form.name);
    std::memcpy(rec + 36,  &form.job,       4);
    std::memcpy(rec + 44,  &form.faction,   4);
    std::memcpy(rec + 48,  &form.face,      4);
    std::memcpy(rec + 52,  &form.hairColor, 4);
    std::memcpy(rec + 216, &lookPresetId,   4);

    if (!SendFrame(nc, 17, payload.data(), payload.size())) return kCharSelectErrSend;

    // Réponse RÉELLE 10093 o : [1][code:4][fiche-écho:10088] — le serveur renvoie la
    // fiche créée (le binaire la recopie dans son miroir liste-de-personnages
    // unk_1669380+10088*slot, non modélisé côté port). On consomme intégralement le
    // flux pour ne pas désynchroniser les lectures suivantes ; seul le code est
    // exploité ici (TODO si un miroir client de la liste des personnages est ajouté).
    if (!RecvExact(nc, 10093)) return kCharSelectErrRecv;
    return ReadResultCode(nc);
}

int32_t CharSlotAction(NetClient& nc, int32_t slot, int32_t action, int32_t arg) {
    // Opcode 18. Payload RÉEL 12 o = 3 champs 4o (slot/action/arg) à 0/4/8 — confirmé
    // par décompilation fraîche de 0x52A740.
    uint8_t payload[12];
    std::memcpy(payload + 0, &slot,   4);
    std::memcpy(payload + 4, &action, 4);
    std::memcpy(payload + 8, &arg,    4);
    if (!SendFrame(nc, 18, payload, sizeof(payload))) return kCharSelectErrSend;
    if (!RecvExact(nc, 5)) return kCharSelectErrRecv; // [1][code:4], confirmé inchangé
    return ReadResultCode(nc);
}

game::EnterCharInfoResult ReqEnterCharInfo(NetClient& nc, int32_t slot) {
    game::EnterCharInfoResult out{};
    // Opcode 22. Payload RÉEL 4 o = SEUL le slot — confirmé par décompilation fraîche
    // de 0x52B070 (l'ancien code envoyait un 2e champ superflu, désalignant la trame
    // de 4 o face à un vrai serveur).
    int32_t slotField = slot;
    if (!SendFrame(nc, 22, &slotField, sizeof(slotField))) {
        out.resultCode = kCharSelectErrSend;
        return out;
    }
    // Réponse [1][code:4][domainId:4][gamePort:4][zoneId:4] (17 o) — CONFIRMÉE
    // inchangée par décompilation fraîche (EA 0x52B217-0x52B2FE).
    if (!RecvExact(nc, 17)) { out.resultCode = kCharSelectErrRecv; return out; }
    int32_t code = 0, domainId = 0, gamePort = 0, zoneId = 0;
    std::memcpy(&code,     nc.recvBuf + 1,  4);
    std::memcpy(&domainId, nc.recvBuf + 5,  4);
    std::memcpy(&gamePort, nc.recvBuf + 9,  4);
    std::memcpy(&zoneId,   nc.recvBuf + 13, 4);
    out.resultCode = code;
    out.domainId   = domainId;
    out.gamePort   = gamePort;
    out.zoneId     = zoneId;
    return out;
}

int32_t ReqCancelEnter(NetClient& nc) {
    // Opcode RÉEL 23 (0x17), PAS 21 (0x15) — écart CRITIQUE découvert par
    // décompilation fraîche de 0x52B310 (nom IDA "Net_ReqCancelEnter" confirmé via
    // lookup_funcs, mais l'octet opcode qu'elle émet sur le fil est bien 23 ; l'opcode
    // 21/Net_SendPacket_Op21 générique est un mécanisme SANS RAPPORT, utilisé
    // ailleurs). SANS payload (9 o). PAS d'attente de réponse (fire-and-forget
    // confirmé, comme AccountKeepAlive) : le binaire fait *a1=0 immédiatement après un
    // envoi réussi, sans recv().
    if (!SendFrame(nc, 23, nullptr, 0)) return kCharSelectErrSend;
    return 0;
}

void BuildEnterWorldTail72(float posX, float posY, float posZ, float rotationDeg,
                            uint8_t out[72]) {
    // Reproduit EXACTEMENT Crt_Memset(&dword_1675AA0,0,0x48) + les 5 écritures de
    // champ de Scene_CharSelectUpdate (EA 0x51c765-0x51c7f9), cf. les constantes
    // kTail72Off* déclarées dans CharSelectPackets.h pour la justification décompilée
    // complète (double confirmation via Map_BeginWarpToFactionTown 0x55C510).
    std::memset(out, 0, 72);
    std::memcpy(out + kTail72OffPosX, &posX, sizeof(float));
    std::memcpy(out + kTail72OffPosY, &posY, sizeof(float));
    std::memcpy(out + kTail72OffPosZ, &posZ, sizeof(float));
    std::memcpy(out + kTail72OffRotA, &rotationDeg, sizeof(float));
    std::memcpy(out + kTail72OffRotB, &rotationDeg, sizeof(float));
    // kTail72OffMode/Flag/Pad8 restent à 0 (memset) : les 2 sites décompilés
    // n'écrivent QUE 0 à ces offsets pour un flux EnterWorld (mode=0, variante=0).
}

} // namespace ts2::net

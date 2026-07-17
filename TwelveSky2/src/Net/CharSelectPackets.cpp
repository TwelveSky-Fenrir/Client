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
#include "Net/Rng.h"           // DefaultRng() — flux _holdrand UNIQUE (Rng_Next 0x7603FD)
#include "Game/ClientRuntime.h" // game::g_Client.Var — échappatoire globals (dword_1675898)
#include <cstring>
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
//
// Rng_Next 0x7603FD = rand() de la CRT MSVC : _holdrand = Crt_GetPtd()[5] (0x76D464) ;
// s = 214013*s + 2531011 (EA 0x76040b) ; retour (s >> 16) & 0x7FFF (EA 0x76041e).
// FLUX UNIQUE PARTAGÉ avec tous les autres tirages du client (le binaire n'a qu'un
// seul _holdrand, semé par srand(time(NULL)) en App_Init 0x461C20 EA 0x461C3E) :
// on tape sur net::DefaultRng() (Net/Rng.h) et NON sur std::rand(), qui constituait
// ici un SECOND flux indépendant (écart d'ordre/valeurs vs le binaire).
inline int RngNext() { return DefaultRng().Next(); }
inline void MakeNonces(uint32_t& nonce1, uint32_t& nonce2) {
    int a = RngNext() % 10000;
    int b = RngNext() % 10000;
    nonce1 = static_cast<uint32_t>(a * b);
    int c = RngNext() % 10000;
    int d = RngNext() % 10000;
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
//
// ORDRE D'ÉCRITURE RE-VÉRIFIÉ À L'OCTET (décompilation 0x52A4A0 + 0x52BD80, cette
// session) — identique dans les 6 builders : `Crt_Memcpy(buf, &nonce1, 4u)` [0..3] ;
// `Crt_Memcpy(v14/*@4*/, &nonce2, 4u)` [4..7] ; `Crt_Memcpy(v15/*@7*/, seq, 1u)` [7,
// ÉCRASE le 4e octet du nonce2) ; `v15[1] = opcode` [8]. Reproduit tel quel ci-dessous
// (memcpy de 4 o du nonce2 en +4, PUIS pkt[7] = seq qui l'écrase).
//
// `extraResidualBytes` : nombre d'octets envoyés AU-DELÀ de 9+payloadLen, présents dans
// la longueur émise mais JAMAIS écrits par le binaire (pile résiduelle). Utilisé
// UNIQUEMENT par op27 (len=0Eh @0x52BE46 alors que le memcpy s'arrête à l'octet 12) —
// ils sont XORés et envoyés comme les autres. Voir CharSelectPackets.h::AccountReq_op27
// (anomalie 1) et le TODO [0x52BE46] sur leur valeur. Vaut 0 pour tous les autres.
bool SendFrame(NetClient& nc, uint8_t opcode, const void* payload, size_t payloadLen,
               size_t extraResidualBytes = 0) {
    std::vector<uint8_t> pkt(9 + payloadLen + extraResidualBytes);
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);
    std::memcpy(pkt.data() + 0, &nonce1, 4);
    std::memcpy(pkt.data() + 4, &nonce2, 4); // octet [7] écrasé juste après par seq
    pkt[7] = nc.seq;
    pkt[8] = opcode;
    if (payloadLen)
        std::memcpy(pkt.data() + 9, payload, payloadLen);
    // Les `extraResidualBytes` restent à 0 (valeur du vector) : ce sont les octets que le
    // binaire laisse à la pile — non déterministes en statique, cf. TODO [0x52BE46].
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
    // +40 = RACE EFFECTIVE de la LISTE. Champ rempli par le SERVEUR uniquement (écho de
    // l'opcode 17) : data_refs(0x16709E0) = 0 réf — le formulaire de création ne l'écrit
    // JAMAIS (re-vérifié cette session : `xrefs_to(0x16709E0)` -> aucune référence).
    // DEUX consommateurs le lisent dans le binaire, tous deux portés en C++ :
    //   · résolution de motion : `imul eax, 2768h` @0x51C528 puis
    //     `mov ecx, ds:dword_16693A8[eax]` @0x51C52E (= &unk_1669380 + 0x2768*slot + 0x28)
    //     -> `call PcModel_ResolveSlotAndApply` (0x4E5A00) @0x51C53A.
    //     ⚠️ L'EA 0x51C555 parfois citée pour cette lecture est FAUSSE : c'est un
    //     `jnz short loc_51C5C1` de comparaison flottante sur +0xF59C (animTime).
    //   · publication du « self » : `Crt_Memcpy(g_SelfCharInvBlock 0x1673170,
    //     &unk_1669380 + 0x2768*slot, 0x2768)` @0x51C707 -> le bloc+0x28 EST
    //     g_LocalElementSecondary 0x1673198 (0x1673198 - 0x1673170 = 0x28 = 40), global à
    //     50 lecteurs dans le jeu (AutoPlay_IsLocalElementMatch 0x45C590,
    //     Char_BuildEquipSnapshot 0x4CC1C0, cGameHud_Render 0x64A900…).
    // Sans cette recopie, game::CharSlotInfo::race restait à 0 et les DEUX chemins C++
    // vivants partaient sur race=0 (Game/CharSelectFlow.cpp::ListMotionFrameCount et
    // UI/LoginScene.cpp::PublishSelfFromSlot -> g_World.self.elementSecondary).
    // Cf. aussi Char_RenderModel branche LISTE : `mov eax,[edx+28h]` @0x527536.
    std::memcpy(&out.race,       rec + kCharRecFieldRace,    4);
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

CharRecordListFields ReadCharRecordListFields(const uint8_t* rec) {
    CharRecordListFields out{};
    // Champs lus par la branche LISTE de Char_RenderModel 0x527020 (arg_4 == 0), dans
    // l'ordre du binaire :
    //   `mov eax, [edx+28h]` @0x527536 -> a2 de PcModel_ResolveEquipSlot = +40 (RACE).
    //     ⚠️ La branche CRÉATION lit +36 au même rôle (`mov edx,[ecx+24h]` @0x527051) —
    //     ce sont bien DEUX champs distincts, cf. l'ancre de kCharRecFieldRace (.h).
    //   `mov ecx, [eax+2Ch]` @0x52752F -> a3 = +44 (GENRE 0..1).
    //   `cmp dword ptr [ecx+24h], 3` @0x52754A -> sentinelle sur +36.
    //   `mov edx, [ecx+0D8h]` @0x527497 + MobDb_GetEntry(mITEM) @0x5274A3 -> +216
    //     (ID d'objet de l'arme de départ).
    std::memcpy(&out.race,   rec + kCharRecFieldRace,    4);
    std::memcpy(&out.gender, rec + kCharRecFieldFaction, 4); // +44, « faction » = genre
    std::memcpy(&out.job,    rec + kCharRecFieldJob,     4);
    std::memcpy(&out.startingWeaponItemId, rec + kCharRecFieldStartingWeaponItemId, 4);
    out.jobSentinelIs3 = (out.job == kCharRecJobSentinelValue);
    return out;
}

bool ReadCharRecordListFields(int32_t slot, CharRecordListFields& out) {
    out = CharRecordListFields{};
    // Garde de PORT : le binaire indexe `&unk_1669380 + 10088*slot` sans borne. Les
    // appelants réels bornent déjà slot à [0..2] (this[15715], -1 = aucun) ; on refuse
    // proprement plutôt que de lire hors des 3 fiches.
    if (slot < 0 || slot >= kCharRecordCount) return false;
    out = ReadCharRecordListFields(g_CharRecords[static_cast<size_t>(slot)]);
    return true;
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
                        int32_t startingWeaponItemId) {
    // Opcode 17. Payload RÉEL 10092 o = 4 o slot + 10088 o fiche personnage (confirmé
    // par décompilation fraîche de 0x52A4A0 + de l'appelant Scene_CharSelectOnMouseUp
    // EA 0x526634-0x5267E4 ; trame totale len=10101 @0x52A582 = 9 + 4 + 10088).
    // INVENTAIRE EXHAUSTIF des champs écrits par le formulaire (search_text "_16709|_1670A"
    // sur [0x51B000, 0x527000) = 64 hits : le binaire n'écrit JAMAIS d'autre octet de la
    // fiche de création) — tout le reste part à ZÉRO :
    //   [20..32] nom (13 o, GetWindowTextA @0x526583/0x52658F) · [36] job (0x52537C
    //   aléatoire / 0x5260B2 − / 0x526158 +) · [44] genre, dit « faction » (0x525382 /
    //   0x5261F8 / 0x526280) · [48] face (0x52538C / 0x526305 / 0x52636B) · [52] hairColor
    //   (0x525396 / 0x5263D1 / 0x52643A) · [104] équipement, 208 o mis à 0
    //   (Crt_Memset(...,0,0xD0) @0x526634 — déjà couvert par le zéro-init du vector) ·
    //   [216] startingWeaponItemId (0x52669A..0x52675B).
    // ⚠️ [40] (race) n'est JAMAIS écrit par le client : data_refs(0x16709E0) = 0 réf.
    // C'est le SERVEUR qui le remplit, et il revient par l'écho (recopié plus bas).
    // ⚠️ [56] n'est JAMAIS écrit par le formulaire non plus.
    // ⚠️ `variant` n'est PAS dans la fiche : c'est this[15716] (+0xF590), un champ de
    // SCÈNE ; il n'atteint le réseau QUE via startingWeaponItemId (+216).
    std::vector<uint8_t> payload(4 + 10088, 0);
    std::memcpy(payload.data(), &slot, 4);
    uint8_t* rec = payload.data() + 4;
    CopyFieldN(rec + kCharRecFieldName, 13, form.name);
    std::memcpy(rec + kCharRecFieldJob,     &form.job,       4);
    std::memcpy(rec + kCharRecFieldFaction, &form.faction,   4); // +44 = genre (nom historique)
    std::memcpy(rec + kCharRecFieldFace,    &form.face,      4);
    std::memcpy(rec + kCharRecFieldHair,    &form.hairColor, 4);
    std::memcpy(rec + kCharRecFieldStartingWeaponItemId, &startingWeaponItemId, 4);

    if (!SendFrame(nc, 17, payload.data(), payload.size())) return kCharSelectErrSend;

    // Réponse RÉELLE 10093 o : [1][code:4][fiche-écho:10088] — le serveur renvoie la
    // fiche créée. On consomme intégralement le flux pour ne pas désynchroniser les
    // lectures suivantes.
    if (!RecvExact(nc, 10093)) return kCharSelectErrRecv;

    // MIROIR LISTE-DE-PERSONNAGES — Net_CreateCharacter 0x52A4A0, EA 0x52a71e :
    //   if (!v18) Crt_Memcpy((unsigned int)&unk_1669380 + 10088 * a1,
    //                        &MEMORY[0x8156C5], 0x2768u);
    // Garde `if (!v18)` à l'EA 0x52a700 : la recopie n'a lieu QUE sur code 0. La source
    // &MEMORY[0x8156C5] = recvBuf(0x8156C0) + 5, donc la fiche écho commence à
    // recvBuf+5 (juste après [1][code:4]) et fait 0x2768 = 10088 o = kCharRecordSize.
    // La destination unk_1669380 + 10088*slot est EXACTEMENT le miroir que
    // Net_LoginRequest 0x51B8E0 remplit au login (EA 0x51bc56/0x51bc6d/0x51bc84) —
    // porté ici par net::g_CharRecords (NetClient.h).
    // SANS cette recopie, le personnage créé DISPARAÎT : LoadCharacterSlotsFromRecords
    // relit g_CharRecords[slot] resté à zéro au prochain passage en sous-état Init
    // (Game/CharSelectFlow.cpp), et ParseCharRecord repasse occupied=false (critère
    // `!out.name.empty()`).
    const int32_t code = ReadResultCode(nc);
    if (code == 0 && slot >= 0 && slot < kCharRecordCount) {
        // Bornes : garde de sûreté du PORT (le binaire n'en a pas — il indexe un
        // tableau de 3 fiches noyé dans .data). `slot` vient toujours de
        // FindFirstFreeSlot() (0..2) : aucune divergence de comportement observable.
        std::memcpy(g_CharRecords[static_cast<size_t>(slot)], nc.recvBuf + 5, kCharRecordSize);
    }
    return code;
}

int32_t VerifyCharName(NetClient& nc, int32_t slotEnc, const std::string& name) {
    // Net_ReqVerifyCharName 0x52B4C0 (opcode 24) — suppression de personnage confirmée
    // par saisie du nom. Trame de 62 o (len=62, EA 0x52b59b) = en-tête 9 o + payload
    // 53 o. Offsets prouvés par les positions de pile (buf@esp+0x24 -> offset 0) :
    //   [nonce1:4@0] (EA 0x52b534) · [nonce2:3@4] (EA 0x52b54c) · [seq:1@7] (EA
    //   0x52b562) · [opcode=24:1@8] (v15[1]=24, EA 0x52b56a) · [slotEnc:4@9]
    //   (Crt_Memcpy(v16,&a1,4), v16@esp+0x2D, EA 0x52b57e) · [name:49@13]
    //   (Crt_Memcpy(v17,a2,0x31u), v17@esp+0x31, EA 0x52b593).
    // -> payload = [slotEnc:i32@0][name:49@4], 4+49 = 53 ; 9+53 = 62. ✓
    // Le champ nom est zéro-rempli (le binaire memcpy 0x31=49 o depuis un tampon local
    // `String` alimenté par GetWindowTextA(dword_166900C, String, 49), EA 0x529273).
    uint8_t payload[4 + 49];
    std::memcpy(payload, &slotEnc, 4);
    CopyFieldN(payload + 4, 49, name);

    if (!SendFrame(nc, 24, payload, sizeof(payload))) return kCharSelectErrSend;
    // Réponse bloquante de 5 o = [1][code:4] (boucle `j != 5` EA 0x52b67a ; code lu à
    // recvBuf+1 = &MEMORY[0x8156C1], EA 0x52b702). XOR clé + ++seq après envoi réussi
    // (EA 0x52b5eb / 0x52b675) : assurés par SendFrame.
    if (!RecvExact(nc, 5)) return kCharSelectErrRecv;
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

// --- Assistant PIN / mot de passe secondaire (op13/14/15) — cf. header ---

int32_t SecondaryPasswordSet(NetClient& nc, const uint8_t pin5[5]) { // Net_AccountReq_op13 0x529AA0
    if (!SendFrame(nc, 13, pin5, 5)) return kCharSelectErrSend;      // opcode 13, PIN[5]@9 (len 14)
    if (!RecvExact(nc, 10)) { NetCloseSocket(nc); return kCharSelectErrRecv; } // recv 10
    const int32_t code = ReadResultCode(nc);                        // [1..4]
    if (code == 0) {                                                // @0x529CEC
        g_SecondaryPwRequired = 0;                                  // dword_16692A4 = 0
        std::memcpy(g_StoredSecondaryPw, nc.recvBuf + 5, 5);        // unk_16692A8 <- PIN écho (recv+5)
    }
    return code;
}

int32_t SecondaryPasswordChange(NetClient& nc, const uint8_t oldPin5[5], const uint8_t newPin5[5]) { // Net_AccountReq_op14 0x529D20
    uint8_t payload[10];
    std::memcpy(payload + 0, oldPin5, 5);                           // ancien PIN [9..13]
    std::memcpy(payload + 5, newPin5, 5);                           // nouveau PIN [14..18]
    if (!SendFrame(nc, 14, payload, 10)) return kCharSelectErrSend; // opcode 14 (len 19)
    if (!RecvExact(nc, 10)) { NetCloseSocket(nc); return kCharSelectErrRecv; } // recv 10
    const int32_t code = ReadResultCode(nc);
    if (code == 0) {                                                // @0x529F7E
        g_SecondaryPwRequired = 0;                                  // dword_16692A4 = 0
        std::memcpy(g_StoredSecondaryPw, nc.recvBuf + 5, 5);        // unk_16692A8 <- nouveau PIN écho
    }
    return code;
}

int32_t SecondaryPasswordVerify(NetClient& nc, const uint8_t pin5[5]) { // Net_AccountReq_op15 0x529FB0
    if (!SendFrame(nc, 15, pin5, 5)) return kCharSelectErrSend;      // opcode 15, PIN[5]@9 (len 14)
    if (!RecvExact(nc, 5)) { NetCloseSocket(nc); return kCharSelectErrRecv; } // recv 5
    const int32_t code = ReadResultCode(nc);
    if (code == 0)                                                  // @0x52A1FC
        g_SecondaryPwRequired = 0;                                  // dword_16692A4 = 0 SEUL (pas de maj PIN)
    return code;
}

int32_t AccountReq_op27(NetClient& nc, int32_t arg) {
    // Net_AccountReq_op27 0x52BD80 (opcode 27), émis depuis Scene_CharSelectOnMouseUp
    // @0x523E07. Voir CharSelectPackets.h::AccountReq_op27 pour les 2 anomalies fidèles.
    //
    // ANOMALIE 1 — trame de 14 o dont le 14e est NON INITIALISÉ : `len = 0Eh` @0x52BE46
    // alors que `Crt_Memcpy(v15 /*offset 9*/, &a1, 4u)` @0x52BE3E n'écrit que les octets
    // 9..12. L'octet 13 est de la pile résiduelle, XORé (boucle `i < len` -> 0..13) et
    // envoyé. On le reproduit via extraResidualBytes=1 (émis à 0).
    // TODO [0x52BE46] : valeur réelle de l'octet 13 non déterminable en statique
    // (pile non initialisée) — dump runtime x32dbg requis pour la connaître.
    if (!SendFrame(nc, 27, &arg, sizeof(arg), /*extraResidualBytes=*/1))
        return kCharSelectErrSend;

    // Réponse de 9 o = [1][code:4][valeur:4] (boucle `j != 9` @0x52BF27).
    if (!RecvExact(nc, 9)) return kCharSelectErrRecv;

    // ANOMALIE 2 — `Crt_Memcpy(&dword_1675898, &MEMORY[0x8156C5], 4u)` @0x52BFC4 est
    // INCONDITIONNEL et précède `*a2 = v16` @0x52BFD2 : AUCUNE garde `if (!code)`,
    // contrairement à op17 (`if (!v18)` @0x52A700). Le champ est donc écrasé même sur
    // erreur serveur — reproduit tel quel (ne pas ajouter de garde).
    // dword_1675898 : longue traîne de globals, même convention que les autres écrivains
    // de cette adresse (Net/GameHandlers_Misc.cpp, Net/GameVarDispatch.cpp).
    int32_t value = 0;
    std::memcpy(&value, nc.recvBuf + 5, 4);
    game::g_Client.Var(0x1675898) = value;

    // Code résultat lu à rx+1 (`Crt_Memcpy(&v16, &MEMORY[0x8156C1], 4u)` @0x52BFB0).
    return ReadResultCode(nc);
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

// Net/CharSelectPackets.h — requêtes réseau BLOQUANTES de l'écran de sélection de
// personnage (scène 4), sur la socket LOGIN encore active (avant ConnectGameServer /
// WSAAsyncSelect — cf. Net/Login.cpp : le même schéma bloquant send()+recv() y est
// déjà utilisé pour LoginRequest). Point d'intégration réseau de
// Game/CharSelectFlow.h::CharSelectHost, appelé depuis UI/LoginScene.cpp.
//
// Fonctions d'origine (renommées dans l'IDB `RE/TwelveSky2.exe.i64`, noms confirmés
// via `lookup_funcs` — voir aussi Docs/TS2_CHARSELECT_AUDIT.md et
// Game/CharSelectFlow.h qui documentait déjà ces mêmes EAs) :
//   Net_AccountKeepAlive 0x5298F0 (opcode 12)
//   Net_CreateCharacter  0x52A4A0 (opcode 17 / 0x11)
//   Net_CharSlotAction   0x52A740 (opcode 18 / 0x12), via CharSelect_ReqDeleteChar 0x528FD0
//   Net_ReqEnterCharInfo 0x52B070 (opcode 22 / 0x16)
//   Net_ReqCancelEnter   0x52B310 (opcode 23 / 0x17 — PAS 21, cf. RECONFIRMATION ci-dessous)
//
// RECONFIRMATION RE (2026-07-14, accès idaTs2 direct — HTTP JSON-RPC 127.0.0.1:13337,
// outil `decompile`, sur les 5 EAs ci-dessus + Scene_CharSelectOnMouseUp 0x522E50 pour
// le remplissage de la fiche de création). La session précédente (sans accès IDA)
// avait supposé que ces 5 fonctions étaient de simples enveloppes autour des builders
// génériques bas niveau `Net_SendPacket_Op12/17/18/21/22` (`RE/net_builders_decomp.json`).
// C'ÉTAIT FAUX : chacune des 5 fonctions construit sa PROPRE trame inline (même motif
// dupliqué — nonces/en-tête/XOR/send — mais avec un opcode et/ou une charge utile
// DIFFÉRENTS des builders génériques homonymes, qui servent en réalité à d'AUTRES
// points d'appel du jeu). Écarts corrigés dans ce module :
//   - Net_AccountKeepAlive : AUCUNE charge utile (9 o, pas 213), et surtout AUCUNE
//     ATTENTE DE RÉPONSE (le binaire ne fait PAS de recv() — heartbeat fire-and-forget,
//     *a1=0 immédiatement après envoi réussi). L'ancien code attendait à tort 5 o.
//   - Net_CreateCharacter : charge utile RÉELLE de 10092 o (4 o slot + 10088 o fiche
//     personnage), PAS 61 o. Réponse RÉELLE de 10093 o ([1][code:4][fiche-écho:10088],
//     le serveur renvoie la fiche créée), PAS 5 o. Offsets connus DANS la fiche
//     (relatifs au début, confirmés par décompilation de l'appelant
//     Scene_CharSelectOnMouseUp EA 0x526634-0x5267E4, cohérents avec les commentaires
//     déjà présents dans Game/CharSelectFlow.h::CharCreateForm) :
//       [20..32] nom (13 o) · [36] job · [44] faction · [48] face · [52] hairColor ·
//       [216] lookPresetId (résolu). Tout le reste est ZÉRO dans tous les chemins
//       observés (jamais écrit avant l'envoi dans le binaire).
//   - Net_CharSlotAction : charge utile RÉELLE de 12 o (3 champs 4o à 0/4/8), PAS 76 o.
//     Réponse [1][code:4] (5 o) inchangée/confirmée.
//   - Net_ReqEnterCharInfo : charge utile RÉELLE de 4 o (SEUL le slot), PAS 2 champs/8o
//     (l'ancien code envoyait un octet superflu qui aurait désaligné la trame de 4 o
//     face à un vrai serveur). Réponse [1][code:4][domainId:4][gamePort:4][zoneId:4]
//     (17 o) CONFIRMÉE inchangée — c'était déjà correct.
//   - Net_ReqCancelEnter : opcode RÉEL 23 (0x17), PAS 21 (0x15) — l'opcode 21/
//     Net_SendPacket_Op21 générique existe bien dans le binaire (utilisé ailleurs,
//     p.ex. World_LoadMap après échec de Net_ConnectGameServer) mais N'EST PAS ce que
//     cette fonction envoie. Toujours SANS payload (9 o). Et surtout AUCUNE ATTENTE DE
//     RÉPONSE (comme AccountKeepAlive) — l'ancien code attendait à tort 5 o.
// Le FRAMING générique (en-tête 9o : nonces/séquence/opcode, puis XOR intégral,
// incrément de séquence après envoi réussi) reste CONFIRMÉ inchangé — seule la
// construction interne (opcode exact + contenu/taille de charge utile + présence ou
// non d'un recv()) a été corrigée.
#pragma once
#include "Net/NetClient.h"
#include "Game/CharSelectFlow.h"
#include <array>
#include <cstdint>
#include <string>

namespace ts2::net {

// --- Layout interne d'une fiche personnage de 10088 o (0x2768) ---
// Une fiche par emplacement (net::g_CharRecords[i], persistée par
// Net/Login.cpp::LoginRequest), MÊME structure que le payload envoyé par
// Net_CreateCharacter (opcode 17, cf. mapping ci-dessus). Offsets RE-CONFIRMÉS par
// décompilation directe de Scene_CharSelectUpdate 0x51BD90 (session 2026-07-14,
// EA 0x51c2f7-0x51c7d4) : le binaire compare/lit `unk_1669394`/`dword_16693B8`/
// `dword_166A8DC`/`dword_166A8E0`/`E4`/`E8`, tous des OFFSETS FIXES relatifs à la base
// `unk_1669380` de la 1ère fiche (stride 2522 dwords = 10088 o = kCharRecordSize) —
// soustraction d'adresses : nom=+20, power=+56, zoneId=+5468, position=+5472/5476/5480.
inline constexpr int kCharRecFieldName    = 20;   // 13 o, C-string (nom du personnage)
inline constexpr int kCharRecFieldJob     = 36;   // int32
inline constexpr int kCharRecFieldFaction = 44;   // int32
inline constexpr int kCharRecFieldFace    = 48;   // int32
inline constexpr int kCharRecFieldHair    = 52;   // int32
inline constexpr int kCharRecFieldPower   = 56;   // int32 — dword_16693B8[2522*i] (unk_1669380+0x38)
inline constexpr int kCharRecFieldPreset  = 216;  // int32 — lookPresetId (résolu job×variant)
inline constexpr int kCharRecFieldZoneId  = 5468; // int32 — dword_166A8DC[2522*i] (+0x155C)
inline constexpr int kCharRecFieldPosX    = 5472; // int32 — dword_166A8E0[2522*i], casté en float à l'usage
inline constexpr int kCharRecFieldPosY    = 5476; // int32 — dword_166A8E4[2522*i]
inline constexpr int kCharRecFieldPosZ    = 5480; // int32 — dword_166A8E8[2522*i]

// Parse une fiche brute de kCharRecordSize (10088) octets en un CharSlotInfo.
// `occupied` reproduit EXACTEMENT le critère du binaire (Crt_Strcmp(name,"") != 0,
// EA 0x51c2f7) : une fiche vide (nom vide) laisse tous les autres champs à leur valeur
// par défaut (le binaire ne les exploite jamais dans ce cas — aucun octet à zéro n'est
// interprété comme job/faction/etc pour un emplacement libre).
void ParseCharRecord(const uint8_t* rec, game::CharSlotInfo& out);

// Peuple `slots` à partir des 3 fiches persistées par Net_LoginRequest
// (net::g_CharRecords, cf. NetClient.h). Point d'intégration réel de
// CharSelectHost::LoadCharacterSlots (Game/CharSelectFlow.h) — branché depuis
// UI/LoginScene.cpp::BuildCharSelectHost.
void LoadCharacterSlotsFromRecords(std::array<game::CharSlotInfo, game::kMaxCharSlots>& slots);

// Codes de transport génériques (miroir de kLoginErrSend/kLoginErrRecv, Net/Login.h) —
// renvoyés quand le send()/recv() bloquant échoue avant même d'obtenir une réponse
// serveur. Consommés par CharSelectFlow.cpp comme n'importe quel « code inconnu »
// (branche `default:` — no-op fidèle, cf. Game/CharSelectFlow.cpp). Valeurs 101/102
// CONFIRMÉES par décompilation (Net_CloseSocket puis *a=101 sur échec send, *a=102 sur
// échec recv, dans les 5 fonctions d'origine).
inline constexpr int32_t kCharSelectErrSend = 101;
inline constexpr int32_t kCharSelectErrRecv = 102;

// Net_AccountKeepAlive 0x5298F0 (opcode 12). Heartbeat de session (/30 frames en
// sous-état Actif de CharSelect, cf. CharSelectFlow.h). SANS payload, SANS attente de
// réponse (fire-and-forget confirmé) : renvoie 0 dès que l'envoi a réussi, 101 sinon.
int32_t AccountKeepAlive(NetClient& nc);

// Net_CreateCharacter 0x52A4A0 (opcode 17). `lookPresetId` = id résolu côté client
// par CharSelectFlow (table job×variant EXACTE, cf. ResolveLookPresetId). Envoie la
// fiche de 10088 o (voir mapping d'offsets ci-dessus) ; consomme intégralement la
// réponse de 10093 o = [1][code:4][fiche-écho:10088].
// MIROIR (EA 0x52a71e, garde `if (!v18)` EA 0x52a700) : sur code 0, la fiche écho
// (recvBuf+5, 10088 o) est recopiée dans g_CharRecords[slot] — port fidèle du miroir
// `unk_1669380 + 10088*slot` du binaire, le MÊME tableau que Net_LoginRequest 0x51B8E0
// remplit au login. Sans cette recopie, LoadCharacterSlotsFromRecords relit une fiche
// à zéro au prochain sous-état Init et le personnage créé disparaît.
int32_t CreateCharacter(NetClient& nc, int32_t slot, const game::CharCreateForm& form,
                        int32_t lookPresetId);

// Net_CharSlotAction 0x52A740 (opcode 18). Deux actions PROUVÉES, deux appelants
// distincts (tous deux atteints depuis UI_MsgBox_OnLButtonUp 0x5C0A90) :
//   action=1, arg=0        -> CharSelect_ReqDeleteChar   0x528FD0 (EA 0x528fee) : suppression
//   action=2, arg=listIndex -> CharSelect_ReqRestoreChar 0x5295D0 (EA 0x5295f6) : restauration
// SÉMANTIQUE DE `arg` PROUVÉE (elle était notée « libre / hors périmètre » ici) :
// c'est le champ +0xF560 (= this[15704]) de la scène CharSelect = un INDEX DE
// SÉLECTION dans la liste de restauration, initialisé à -1 (EA 0x51c1e2), piloté par
// deux boutons flèche clampés — précédent `if (idx > 0) --idx` (EA 0x524232-0x524250)
// et suivant `if (idx < count-1) ++idx` avec count = champ +0xF3C8 (EA 0x5242ac-
// 0x5242d8) — remis à 0 à l'EA 0x525c2d, relu par Scene_CharSelectRender (EA
// 0x52030f/0x52044f). Ce n'est NI une constante NI un drapeau.
int32_t CharSlotAction(NetClient& nc, int32_t slot, int32_t action, int32_t arg);

// Net_ReqVerifyCharName 0x52B4C0 (opcode 24) — suppression de personnage confirmée par
// SAISIE DU NOM (chemin distinct de l'opcode 18 action=1 : c'est un second mécanisme de
// suppression, à double confirmation). Appelée par CharSelect_ReqDeleteCharByName
// 0x529230 (EA 0x5292cd), elle-même atteinte UNIQUEMENT depuis UI_MsgBox_OnLButtonUp
// 0x5C0A90 case 41 (EA 0x5c1743).
// Trame 62 o = en-tête 9 o + [slotEnc:i32@0][name:49@4] (53 o) ; réponse 5 o
// = [1][code:4]. Codes routés par l'appelant : 0/1/2/3/4/5/101/102/default.
//
// `slotEnc` est un slot ENCODÉ, PAS le slot brut — EA 0x5292cd :
//   Net_ReqVerifyCharName(*(_BYTE *)(this + 62860) + 100 * *(_BYTE *)(this + 62848), ...)
// soit `slot(+0xF58C) + 100 * flag(+0xF580)`, les DEUX lus en _BYTE. Voir
// game::ConfirmDeleteCharByName (Game/CharSelectFlow.h) pour la preuve d'atteignabilité
// qui fixe flag==1 sur tout envoi réel.
int32_t VerifyCharName(NetClient& nc, int32_t slotEnc, const std::string& name);

// Net_ReqEnterCharInfo 0x52B070 (opcode 22). Résultat COMPLET (resultCode/domainId/
// gamePort/zoneId), directement au format attendu par CharSelectHost::RequestEnterCharInfo.
game::EnterCharInfoResult ReqEnterCharInfo(NetClient& nc, int32_t slot);

// Net_ReqCancelEnter 0x52B310 (opcode 23 — cf. RECONFIRMATION ci-dessus — SANS
// payload). Annule une entrée après échec de connexion récupérable (codes 3/4/5 de
// ConnectToGameServer, cf. CharSelectFlow.cpp). SANS attente de réponse (fire-and-
// forget confirmé, comme AccountKeepAlive) : renvoie 0 dès que l'envoi a réussi.
int32_t ReqCancelEnter(NetClient& nc);

// --- Layout CONFIRMÉ du "struct72" partagé (Op12.a4 / Op15.a2 / Op16.a2) ---
// RE-CONFIRMÉ par décompilation fraîche (2026-07-14, accès idaTs2 direct) de
// Scene_CharSelectUpdate 0x51BD90 (EA 0x51c765-0x51c7f9, écrivain du record juste
// avant Net_ConnectGameServer) ET, indépendamment, de Map_BeginWarpToFactionTown
// 0x55C510 (EA 0x55c6a9-0x55c65a, MÊME motif octet-pour-octet sur le MÊME global
// `dword_1675AA0`) — les deux sites confirment le même layout, ce qui élimine tout
// doute sur sa nature de "record de téléportation/spawn" générique :
//   Crt_Memset(&dword_1675AA0, 0, 0x48);   // 72 o à zéro
//   dword_1675AA0 = 0;                     // +0x00 i32  mode/type (0 dans les 2 sites EnterWorld/warp ville)
//   dword_1675AA4 = 0;                     // +0x04 i32  variante (0 ou 1 selon le site appelant, 0 ici)
//   flt_1675AA8   = 0.0f;                  // +0x08 f32  toujours 0.0 dans tous les sites observés
//   flt_1675AAC   = posX;                  // +0x0C f32  position de spawn X (PAS +0x00 !)
//   flt_1675AB0   = posY;                  // +0x10 f32  position de spawn Y
//   flt_1675AB4   = posZ;                  // +0x14 f32  position de spawn Z
//   // +0x18..+0x23 (12 o) : jamais écrits par ces 2 sites -> restent à 0 (memset)
//   flt_1675AC4   = (Rng_Next() % 360);    // +0x24 f32  rotation de spawn (0..359, tirée fraîchement)
//   flt_1675AC8   = flt_1675AC4;           // +0x28 f32  MÊME valeur dupliquée (pas un 2e angle)
//   // +0x2C..+0x47 (28 o) : jamais écrits par ces 2 sites -> restent à 0 (memset)
// Écart corrigé vs. l'ancien câblage (Scene/SceneManager.cpp, host.SendEnterWorldRequest,
// avant cette session) : spawnX/Y/Z étaient sérialisés à tort aux offsets +0x00/+0x04/+0x08
// (avec le champ mode/type et le padding flottant écrasés par la position) au lieu de
// +0x0C/+0x10/+0x14, et la rotation (+0x24/+0x28) n'était pas envoyée du tout (laissée à
// zéro). Cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md pour le détail complet de la vérification.
inline constexpr int kTail72OffMode  = 0x00; // i32, 0 pour EnterWorld
inline constexpr int kTail72OffFlag  = 0x04; // i32, 0 pour EnterWorld
inline constexpr int kTail72OffPad8  = 0x08; // f32, toujours 0.0
inline constexpr int kTail72OffPosX  = 0x0C; // f32
inline constexpr int kTail72OffPosY  = 0x10; // f32
inline constexpr int kTail72OffPosZ  = 0x14; // f32
inline constexpr int kTail72OffRotA  = 0x24; // f32, rotation (Rng_Next() % 360)
inline constexpr int kTail72OffRotB  = 0x28; // f32, duplicata de kTail72OffRotA

// Construit le bloc struct72 EXACT (72 o, zéro-rempli puis les 5 champs ci-dessus
// posés aux offsets confirmés) pour le payload tail72 de Net_SendPacket_Op12
// (opcode 12, EnterWorld). `out` doit pointer vers 72 octets valides.
void BuildEnterWorldTail72(float posX, float posY, float posZ, float rotationDeg,
                            uint8_t out[72]);

} // namespace ts2::net

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
// +36 : champ « job/classe » ÉCRIT PAR LE FORMULAIRE DE CRÉATION (dword_16709DC,
// 3 écritures : 0x52537C aléatoire / 0x5260B2 flèche − / 0x526158 flèche +) et lu par
// l'aperçu de CRÉATION comme index de race. DISTINCT de kCharRecFieldRace (+40) — voir
// l'ancre détaillée sur celui-ci. Dans la branche LISTE, +36 ne sert QUE de SENTINELLE
// testée `== 3` (Char_RenderModel 0x527020, `cmp dword ptr [ecx+24h], 3` @0x52754A).
inline constexpr int kCharRecFieldJob     = 36;   // int32
// +40 : RACE EFFECTIVE de la LISTE — index passé à PcModel_ResolveEquipSlot 0x4E46A0
// (a2) pour résoudre le modèle 3D, et à PcSnd_ResolveEquipSlot (@0x5251E4) pour le son.
// ANCRE DÉCISIVE — Char_RenderModel 0x527020 a DEUX branches (`cmp [ebp+arg_4], 0 ;
// jz loc_527452` @0x52702F) qui lisent des champs DIFFÉRENTS :
//   branche CRÉATION (arg_4 != 0) : `mov edx, [ecx+24h]` @0x527051 -> a2 = record+36
//   branche LISTE    (arg_4 == 0) : `mov eax, [edx+28h]` @0x527536 -> a2 = record+40
// (les DEUX passent record+44 en a3 : `mov eax,[edx+2Ch]` @0x52704A / `mov ecx,[eax+2Ch]`
// @0x52752F). Ce n'est PAS un copier-coller : la branche LISTE lit AUSSI +36, mais comme
// sentinelle `== 3` @0x52754A -> les deux champs COEXISTENT avec des rôles distincts.
// RE-VÉRIFIÉ (désassemblage direct, cette session) + corroboré par data_refs :
//   data_refs(0x16709E0) = fiche de création +40 -> ZÉRO référence : le client n'écrit
//     JAMAIS +40 ; c'est le SERVEUR qui le remplit (écho op17 @0x52A71E).
//   data_refs(0x16693A8) = fiche de liste +40 -> 5 réfs, TOUTES en lecture (0x51C52E,
//     0x51C598, 0x51C622, 0x51C691 dans Scene_CharSelectUpdate ; 0x5251D8 dans
//     Scene_CharSelectOnMouseUp).
// => Rendre la LISTE avec +36 (= job) affiche le MAUVAIS modèle 3D. Cf.
// ReadCharRecordListFields ci-dessous, l'accès à utiliser côté rendu de la liste.
inline constexpr int kCharRecFieldRace    = 40;   // int32 — g_LocalElementSecondary 0x1673198
// +44 : GENRE (0..1) — nom historique « faction » conservé pour ne pas casser
// game::CharSlotInfo::faction (Game/CharSelectFlow.h, hors de ce front). PcModel_ResolveEquipSlot
// 0x4E46A0 borne a2>2 et a3>1 (@0x4E46CC) => a2(race) ∈ [0..2], a3(genre) ∈ [0..1].
// L'OFFSET d'émission est correct : divergence de NOMMAGE uniquement, ne pas y toucher.
inline constexpr int kCharRecFieldFaction = 44;   // int32 — genre en réalité
inline constexpr int kCharRecFieldFace    = 48;   // int32
inline constexpr int kCharRecFieldHair    = 52;   // int32
// +56 : le binaire l'utilise comme NIVEAU (g_SelfLevel 0x16731A8 : tier, affichage du
// niveau, sélection par défaut). Nom « power » conservé — game::CharSlotInfo::power est
// hors de ce front. Divergence de NOMMAGE uniquement, la valeur et l'usage sont corrects.
inline constexpr int kCharRecFieldPower   = 56;   // int32 — dword_16693B8[2522*i] (unk_1669380+0x38)
// +216 : ID D'OBJET DE L'ARME DE DÉPART, résolu dans la DB items — PAS un « lookPresetId ».
// ANCRE : Char_RenderModel branche LISTE, `mov edx, [ecx+0D8h]` @0x527497 puis
// `mov ecx, offset mITEM ; call MobDb_GetEntry` @0x52749E/0x5274A3 — EXACTEMENT le même
// motif que les 8 autres slots d'équipement (+0x78/+0x88/+0xB8/+0xE8/+0xF8/+0x108/+0x118/
// +0x128 ; p.ex. +0xE8 -> MobDb_GetEntry @0x5274BA, 2 instructions plus loin).
// La FORMULE de résolution côté client reste `6*race + variant + 5` (bornes 5..19,
// cf. game::ResolveLookPresetId) — seul le NOM du champ était faux.
// ⚠️ +216 n'est écrit QU'À LA CONFIRMATION de la création (0x52669A..0x52675B) : l'aperçu
// 3D du FORMULAIRE lit son arme sur la SCÈNE (this[15716] = +0xF590, `mov edx,[ecx+0F590h]`
// @0x5271B8), PAS ici. Ne pas câbler +216 sur l'aperçu de création.
inline constexpr int kCharRecFieldStartingWeaponItemId = 216;  // int32
inline constexpr int kCharRecFieldZoneId  = 5468; // int32 — dword_166A8DC[2522*i] (+0x155C)
inline constexpr int kCharRecFieldPosX    = 5472; // int32 — dword_166A8E0[2522*i], casté en float à l'usage
inline constexpr int kCharRecFieldPosY    = 5476; // int32 — dword_166A8E4[2522*i]
inline constexpr int kCharRecFieldPosZ    = 5480; // int32 — dword_166A8E8[2522*i]

// Valeur de la SENTINELLE testée sur +36 par la branche LISTE de Char_RenderModel :
// `cmp dword ptr [ecx+24h], 3 ; jnz loc_527669` @0x52754A/0x52754E. Si +36 != 3, le bloc
// 0x527554..0x527669 est SAUTÉ. Sémantique du « 3 » NON PROUVÉE (on sait seulement
// QUE le binaire teste cette valeur, pas POURQUOI).
// TODO [0x52754A] : élucider ce que le bloc gardé dessine réellement (descendre
// loc_527554..loc_527669) — nécessaire pour savoir ce qui disparaît quand +36 != 3.
inline constexpr int32_t kCharRecJobSentinelValue = 3;

// --- Champs de la fiche consommés par le RENDU DE LA LISTE (écran this[15714]==1) ---
// Vue en LECTURE SEULE des champs que Char_RenderModel 0x527020 lit dans sa branche
// LISTE (arg_4 == 0). Existe parce que game::CharSlotInfo (Game/CharSelectFlow.h, HORS
// de ce front) n'expose NI la race (+40), NI l'arme de départ (+216), NI la sentinelle
// (+36==3) : son champ `job` porte +36, qui n'est PAS la race de la liste (cf. l'ancre
// de kCharRecFieldRace). Le rendu de la liste doit lire ICI, pas dans CharSlotInfo::job.
struct CharRecordListFields {
    int32_t race                 = 0;     // +40 — a2 de PcModel_ResolveEquipSlot @0x527536
    int32_t gender               = 0;     // +44 — a3 de PcModel_ResolveEquipSlot @0x52752F
    int32_t startingWeaponItemId = 0;     // +216 — MobDb_GetEntry(mITEM) @0x5274A3
    int32_t job                  = 0;     // +36 — sentinelle uniquement dans la liste
    bool    jobSentinelIs3        = false; // (+36 == 3) @0x52754A
};

// Lit les champs ci-dessus dans une fiche brute de kCharRecordSize octets.
CharRecordListFields ReadCharRecordListFields(const uint8_t* rec);

// Idem depuis net::g_CharRecords[slot]. Renvoie false (et laisse `out` par défaut) si
// `slot` est hors [0, kCharRecordCount) — garde de PORT (le binaire indexe sans borne).
bool ReadCharRecordListFields(int32_t slot, CharRecordListFields& out);

// Parse une fiche brute de kCharRecordSize (10088) octets en un CharSlotInfo.
// `occupied` reproduit EXACTEMENT le critère du binaire (Crt_Strcmp(name,"") != 0,
// EA 0x51c2f7) : une fiche vide (nom vide) laisse tous les autres champs à leur valeur
// par défaut (le binaire ne les exploite jamais dans ce cas — aucun octet à zéro n'est
// interprété comme job/faction/etc pour un emplacement libre).
//
// ⚠️ `out.job` porte +36 (correct : +36 EST le champ job, cf. kCharRecFieldJob). Ce
// n'est PAS la race du rendu de la LISTE : celle-ci est en +40 et n'est PAS exposée par
// game::CharSlotInfo (struct hors de ce front). Le rendu de la liste doit passer par
// ReadCharRecordListFields ci-dessus. Cf. l'ancre de kCharRecFieldRace (0x527536 vs
// 0x527051) pour la preuve.
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

// Net_CreateCharacter 0x52A4A0 (opcode 17). `startingWeaponItemId` (ex-`lookPresetId`,
// RENOMMÉ : c'est un ID D'OBJET résolu dans la DB items, cf. l'ancre de
// kCharRecFieldStartingWeaponItemId / MobDb_GetEntry @0x5274A3) = id résolu côté client
// par CharSelectFlow (formule `6*race + variant + 5`, bornes 5..19, cf.
// ResolveLookPresetId — la FORMULE est inchangée, seul le nom du champ était faux).
// Envoie la fiche de 10088 o (voir mapping d'offsets ci-dessus) ; consomme intégralement
// la réponse de 10093 o = [1][code:4][fiche-écho:10088].
// TRAME RE-VÉRIFIÉE À L'OCTET (décompilation directe 0x52A4A0, cette session) :
// len=10101 @0x52A582 (= 9 en-tête + 4 slot @9 + 10088 fiche @13) ; recv de 10093
// @0x52A661 ; `Crt_Memcpy(v16 /*offset 9*/, &a1, 4u)` @0x52A562 ; `Crt_Memcpy(v17
// /*offset 13*/, a2, 0x2768u)` @0x52A57A. Aucun octet résiduel (contrairement à op27).
// MIROIR (EA 0x52a71e, garde `if (!v18)` EA 0x52a700) : sur code 0, la fiche écho
// (recvBuf+5, 10088 o) est recopiée dans g_CharRecords[slot] — port fidèle du miroir
// `unk_1669380 + 10088*slot` du binaire, le MÊME tableau que Net_LoginRequest 0x51B8E0
// remplit au login. Sans cette recopie, LoadCharacterSlotsFromRecords relit une fiche
// à zéro au prochain sous-état Init et le personnage créé disparaît.
int32_t CreateCharacter(NetClient& nc, int32_t slot, const game::CharCreateForm& form,
                        int32_t startingWeaponItemId);

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
// SAISIE DU NOM. Appelée par CharSelect_ReqDeleteCharByName 0x529230 (EA 0x5292cd),
// elle-même atteinte UNIQUEMENT depuis UI_MsgBox_OnLButtonUp 0x5C0A90 case 41
// (EA 0x5c1743).
//
// 🔴 CET OPCODE N'EST JAMAIS ÉMIS PAR CE CLIENT — ce n'est PAS un « second mécanisme de
// suppression à double confirmation » (affirmation ERRONÉE des versions antérieures de ce
// commentaire, corrigée ici). Le panneau de suppression par nom NE S'OUVRE JAMAIS :
//   - `var_434` de Scene_CharSelectOnMouseUp, sur la FONCTION ENTIÈRE [0x522E50,0x526B90) :
//     3 références seulement — 0x522E50 (déclaration de frame), `mov [ebp+var_434], 0`
//     @0x525DFA, `cmp [ebp+var_434], 0` @0x525F91. AUCUNE écriture non nulle, AUCUN `lea`
//     (donc aucun aliasing par pointeur) => `var_434 == 0` est INVARIANT => le
//     `jnz 0x525FC0` n'est JAMAIS pris.
//   - conséquence : `this[0xF57C] = 1` (ouverture du panneau) n'est écrit QU'À l'EA
//     0x52601D, dans le bloc mort ; les 3 autres écritures de 0xF57C sont `= 0`.
// La chaîne 0x525FC0 -> 0x529230 -> 0x52B4C0 est donc INATTEIGNABLE en entier.
// => NE CÂBLER AUCUN CLIC vers VerifyCharName. La fonction reste portée (fidélité du
// code présent dans le binaire) mais doit rester MORTE, conformément à la règle
// « une fonction morte dans le binaire reste morte en C++ ».
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

// Net_AccountReq_op27 0x52BD80 (opcode 27). Émis par Scene_CharSelectOnMouseUp
// (`call Net_AccountReq_op27` @0x523E07 — xref UNIQUE, cf. xrefs_to(0x52BD80) = 1 réf),
// donc bien dans la surface d'émission de la scène 4.
//
// ⚠️ NE PAS CONFONDRE avec net::Net_SendPacket_Op27 (Net/SendPackets.h) : celui-là est
// Net_SendPacket_Op27 0x4B5B90, opcode 0x1b, sur la socket de JEU (amélioration d'item).
// Aucun rapport — homonymie d'indice de builder uniquement.
//
// `arg` : le binaire passe this[+0xF0A4] (`mov eax, [edx+0F0A4h]` @0x523DFA), un INDEX DE
// SÉLECTION initialisé à -1 (@0x51C2C0, bloc Init de Scene_CharSelectUpdate), écrit par
// Scene_CharSelectOnMouseDown (@0x521EB7), gardé `!= -1` juste avant l'émission
// (`cmp dword ptr [eax+0F0A4h], 0FFFFFFFFh` @0x523DC1), et remis à -1 après (@0x523E3C,
// @0x524073). Émis sur 4 OCTETS (`Crt_Memcpy(v15, &a1, 4u)` @0x52BE3E) — ce n'est PAS un
// arg 1 octet, malgré le commentaire de l'IDB (« opcode-27 (1-byte arg) ») qui est FAUX.
// TODO [0x521EB7] : sémantique exacte de this[+0xF0A4] (quelle liste ce panneau
// sélectionne) NON PROUVÉE — seul son protocole (index, -1 = aucun) l'est.
//
// 🔴 DEUX ANOMALIES FIDÈLES, à ne PAS « corriger » :
//  1. TRAME DE 14 OCTETS DONT LE 14e EST NON INITIALISÉ. `len = 0Eh` @0x52BE46 (14), mais
//     seuls les octets 0..12 sont écrits (en-tête 9 o + `Crt_Memcpy(payload@9, &a1, 4u)`
//     @0x52BE3E s'arrête à l'octet 12). L'octet 13 est de la PILE RÉSIDUELLE : il est
//     malgré tout XORé (boucle `i < len` couvrant 0..13) et ENVOYÉ. Le layout de frame
//     IDA le confirme : `var_3EF` (_BYTE[995]) commence au frame-offset 0x2D = octet 9,
//     et rien n'écrit son index [4]. Il FAUT émettre 14 octets, sinon la trame est courte
//     de 1 et désaligne le serveur.
//     TODO [0x52BE46] : la VALEUR de l'octet 13 est de la pile non initialisée — non
//     déterministe en statique. On émet 0 (le seul choix reproductible) ; un dump runtime
//     x32dbg serait nécessaire pour connaître la valeur réelle observée.
//  2. `dword_1675898 ← rx+5` est écrit INCONDITIONNELLEMENT (`Crt_Memcpy(&dword_1675898,
//     &MEMORY[0x8156C5], 4u)` @0x52BFC4), AVANT le test du code (`*a2 = v16` @0x52BFD2) —
//     contrairement à TOUS les autres builders, qui gardent leur effet par `if (!code)`
//     (cf. op17 : `if (!v18)` @0x52A700). Aucune garde ici : le champ est écrasé même
//     quand le serveur renvoie une erreur.
// Réponse : recv de 9 o (`j != 9` @0x52BF27) = [1][code:4][valeur:4]. Renvoie le code
// (rx+1, @0x52BFB0).
int32_t AccountReq_op27(NetClient& nc, int32_t arg);

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

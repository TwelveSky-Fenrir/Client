// Game/CharSelectFlow.cpp — implémentation. Voir CharSelectFlow.h pour les EA d'origine,
// l'asymétrie de la machine d'états, et les deux corrections de documentation [H1]/[A12].
#include "Game/CharSelectFlow.h"

// Deux globals d'état client déjà réifiés, touchés DIRECTEMENT (et non via CharSelectHost)
// parce que ce sont les MÊMES globals que le binaire manipule et que la fidélité en dépend :
//   net::g_MorphInProgress = g_MorphInProgress 0x1675A88 — garde d'ANTI-REJEU de la
//     séquence d'entrée (EA 0x51c6bb). Ce n'est pas un flag local : il est aussi lu par
//     Camera_UpdateFromInput (0x50B857, cf. App/PlayerInputController) et écrit par le
//     monde — donc revenir en CharSelect avec le flag encore à 1 DOIT bloquer l'entrée,
//     exactement comme dans le binaire.
//   net::DefaultRng() = Rng_Next 0x7603FD (_holdrand partagé) — l'ORDRE des tirages est
//     observable (nonces des paquets), donc tout tirage doit passer par ce flux unique.
// Précédent établi : Game/ComboPickupTick.h et UI/LoginScene.cpp font déjà exactement ça.
// Ces deux en-têtes sont des feuilles (<cstdint> seul) : aucun cycle avec
// Net/CharSelectPackets.h, qui inclut Game/CharSelectFlow.h.
#include "Net/ClientState.h"
#include "Net/Rng.h"

#include <windows.h> // MultiByteToWideChar — ValidateNameCharset() (Str_ValidateNameChars 0x53FD70)

namespace ts2::game {

// Str_ValidateNameChars 0x53FD70 — voir la doc complète en tête de déclaration (.h).
bool ValidateNameCharset(const std::string& name) {
    // Capacité FIXE de 13 WCHAR (== 12 caractères utiles + NUL), EXACTEMENT comme
    // l'original : `MultiByteToWideChar(0, 0, name, -1, &buf, 13)`.
    wchar_t wbuf[13] = {};
    const int written = MultiByteToWideChar(CP_ACP, 0, name.c_str(), -1, wbuf, 13);
    if (written == 0) return false; // encodage invalide OU nom > 12 caracteres utiles

    for (const wchar_t* p = wbuf; *p; ++p) {
        const unsigned c = static_cast<unsigned>(*p);
        const bool digit = (c >= 0x30 && c <= 0x39);
        const bool upper = (c >= 0x41 && c <= 0x5A);
        const bool lower = (c >= 0x61 && c <= 0x7A);
        const bool thai  = (c >= 0x0E00 && c <= 0x0E7F);
        if (!digit && !upper && !lower && !thai) return false;
    }
    return true; // fidele : un nom VIDE traverse cette boucle et renvoie true ici
}

namespace {

constexpr int32_t kInitWaitFrames          = 30; // 0x1E — `++this[2] >= 0x1Eu` EA 0x51bde4
constexpr int32_t kKeepAliveIntervalFrames = 30; // 0x1E — EA 0x51c40b ET 0x51c46e
// ⚠ AUCUNE ANCRE IDA — artefact de portage, PAS une valeur du binaire. N'est atteint que
// si l'hôte n'implémente AUCUNE résolution de motion (cf. MotionFrameCount). Ne jamais s'en
// servir pour « rattraper » une durée nulle : 0 est une valeur légitime (ancre 0x4d7854).
constexpr float   kDefaultEnterPreviewFrames = 30.0f; // repli si AUCUN hook de durée

// StrTable005 ids EXACTS relevés au désassemblage, avec leur TYPE de NoticeDlg (2e arg de
// UI_NoticeDlg_Open 0x5C0280) — le type décide du devenir de l'état Verrouillé, cf. .h.
constexpr int32_t kStrSessionExpired      = 20;   // type 2 (EA 0x51c42a / LABEL_92 0x51cb02)
constexpr int32_t kStrEnterInvalidName    = 1856; // 0x740, type 1 (EA 0x525117/0x52512e)
constexpr int32_t kStrEnterNoSelection    = 47;   // 0x2F, type 1 (EA 0x5250C4)
constexpr int32_t kStrCreateListFull      = 22;   // 0x16, type 1 (EA 0x525294/0x5252a1/0x5252a8)
// 0x873 — CRÉATION INTERDITE SUR CETTE RÉGION. type 1 (`push 1` EA 0x5252df), notice
// EA 0x5252e6 puis RETURN EA 0x5252eb. Garde : this[15374]==40 ET !g_ServerModeFlag.
constexpr int32_t kStrCreateRegionBlocked = 2163;
constexpr int32_t kStrCreateEmptyName     = 38; // GetWindowTextA==0, EA 0x5265a5
constexpr int32_t kStrCreateInvalidChars  = 39; // !Str_ValidateNameChars, EA 0x5265db
constexpr int32_t kStrCreateBannedWord    = 40; // maybe_Dict001_MatchWord, EA 0x526611
constexpr int32_t kStrCreateSuccess       = 41;
constexpr int32_t kStrCreateFail42        = 42; // -> Locked
constexpr int32_t kStrCreateFail43        = 43;
constexpr int32_t kStrCreateFail701       = 701;
constexpr int32_t kStrCreateFail44        = 44; // -> Locked
constexpr int32_t kStrCreateFail45        = 45; // -> Locked
constexpr int32_t kStrDeleteSuccess       = 50;
constexpr int32_t kStrDeleteFail51        = 51; // -> Locked
constexpr int32_t kStrDeleteFail411       = 411;
constexpr int32_t kStrDeleteFail48        = 48;
constexpr int32_t kStrDeleteFail633       = 633;
constexpr int32_t kStrDeleteFail2091      = 2091;
constexpr int32_t kStrDeleteFail52        = 52; // -> Locked
constexpr int32_t kStrDeleteFail53        = 53; // -> Locked

// --- op22 (Net_ReqEnterCharInfo) : switch EA 0x51c83c ---
constexpr int32_t kStrEnterInfoFail55   = 55;   // code 1, TYPE 1 -> retour Idle (EA 0x51cba4/0x51cbb1)
constexpr int32_t kStrEnterInfoFail1347 = 1347; // code 2, type 2 -> Locked (EA 0x51cbf9/0x51cc0e)
constexpr int32_t kStrEnterInfoFail1348 = 1348; // code 3, type 2 -> Locked (EA 0x51cc33/0x51cc48)
constexpr int32_t kStrEnterInfoFail229  = 229;  // code 4, type 2 -> Locked (EA 0x51cc6d/0x51cc82)
constexpr int32_t kStrEnterInfoFail56   = 56;   // code 101, type 2 -> Locked (EA 0x51cca4/0x51ccb9)
constexpr int32_t kStrEnterInfoFail57   = 57;   // code 102, type 2 -> Locked (EA 0x51ccdb/0x51ccf0)

// --- op11 (Net_ConnectGameServer) : switch EA 0x51c87e ---
constexpr int32_t kStrConnectFail59 = 59; // code 1, type 2 -> Locked (EA 0x51c8b3/0x51c8c8)
constexpr int32_t kStrConnectFail60 = 60; // code 2, type 2 -> Locked (EA 0x51c8ea/0x51c8ff)
constexpr int32_t kStrConnectFail61 = 61; // code 3, TYPE 1 -> CancelEnter (EA 0x51c921/0x51c92e)
constexpr int32_t kStrConnectFail62 = 62; // code 4, TYPE 1 -> CancelEnter (EA 0x51c9ce/0x51c9db)
constexpr int32_t kStrConnectFail63 = 63; // code 5, TYPE 1 -> CancelEnter (EA 0x51ca7b/0x51ca88)
constexpr int32_t kStrConnectFail64 = 64; // code 6, type 2 -> Locked (EA 0x51cb31/0x51cb46)
constexpr int32_t kStrConnectFail65 = 65; // code 7, type 2 -> Locked (EA 0x51cb68/0x51cb7d)

// --- opcode 24 : chaîne MORTE (cf. [H1]) — ids conservés pour le port fidèle ---
constexpr int32_t kStrDeleteByNameEmpty   = 1463; // nom vide, SANS envoi (EA 0x52928c)
constexpr int32_t kStrDeleteByNameOk      = 1464; // code 0   (EA 0x52930b)
constexpr int32_t kStrDeleteByNameFail1   = 1465; // code 1   (EA 0x5293cc)
constexpr int32_t kStrDeleteByNameFail2   = 1468; // code 2   (EA 0x5293f2)
constexpr int32_t kStrDeleteByNameFail3   = 1469; // code 3   (EA 0x529418)
constexpr int32_t kStrDeleteByNameFail4   = 1466; // code 4   (EA 0x52943e)
constexpr int32_t kStrDeleteByNameFail5   = 1470; // code 5   (EA 0x529464)
constexpr int32_t kStrDeleteByNameFail101 = 703;  // code 101 (EA 0x52948a) -> Lock, type 2
constexpr int32_t kStrDeleteByNameFail102 = 704;  // code 102 (EA 0x5294c7) -> Lock, type 2

// --- opcode 18 action=2 : restauration (CharSelect_ReqRestoreChar 0x5295D0) ---
constexpr int32_t kStrRestoreOk          = 1271; // code 0   (EA 0x52962b)
constexpr int32_t kStrRestoreSessLost1   = 51;   // code 1   (EA 0x52967d) -> Lock, type 2
constexpr int32_t kStrRestoreFail1272    = 1272; // code 2   (EA 0x5296b7)
constexpr int32_t kStrRestoreFail2091    = 2091; // code 5   (EA 0x5296dd)
constexpr int32_t kStrRestoreFail2541    = 2541; // code 11  (EA 0x52970e)
constexpr int32_t kStrRestoreFail2542    = 2542; // code 12  (EA 0x52973f)
constexpr int32_t kStrRestoreFail2545    = 2545; // code 13  (EA 0x529770)
constexpr int32_t kStrRestoreFail2543    = 2543; // code 14  (EA 0x5297a1)
constexpr int32_t kStrRestoreFail2544    = 2544; // code 15  (EA 0x5297d2)
constexpr int32_t kStrRestoreSessLost101 = 52;   // code 101 (EA 0x5297f2) -> Lock, type 2
constexpr int32_t kStrRestoreSessLost102 = 53;   // code 102 (EA 0x529826) -> Lock, type 2

// UI_NoticeDlg_Open(byte_18225C8, type, StrTable005_Get(g_LangId, strId), "") 0x5C0280.
// Le TYPE est structurant (cf. NoticeType) : on privilégie le hook typé, et on ne retombe
// sur le hook historique que s'il n'y a pas mieux — auquel cas le type est PERDU.
inline void Notice(const CharSelectHost& host, int32_t strId, NoticeType type) {
    if (host.ShowNoticeTyped) { host.ShowNoticeTyped(strId, type); return; }
    if (host.ShowNotice)      host.ShowNotice(strId);
}

// `*(this+1) = 2 ; *(this+2) = 0` — le motif EXACT des 33 sites d'erreur verrouillante
// (13 dans Update : 0x51c432, 0x51c8c8, 0x51c8ff, 0x51cb0a, 0x51cb46, 0x51cb7d, 0x51cc0e,
// 0x51cc48, 0x51cc82, 0x51ccb9, 0x51ccf0 … ; 20 dans OnMouseUp).
inline void Lock(CharSelectState& s) {
    s.subState     = CharSelectSubState::Locked;
    s.frameCounter = 0;
}

// Table EXACTE de l'id d'ARME DE DÉPART (fiche +216 = dword_1670A90), relevée à
// 0x5266CD-0x526760 : switch externe sur dword_16709DC (job) -> 3 blocs, chacun un switch
// interne sur this[15716] (variant, `[this+0xF590]` EA 0x526671/0x5266C7/0x52671A).
// Valeurs écrites : 5/6/7 (job 0, EA 0x52669A/0x5266A6/0x5266B2), 11/12/13 (job 1,
// EA 0x5266F0/0x5266FC/0x526708), 17/18/19 (job 2, EA 0x526743/0x52674F/0x52675B).
// Équivalent fermé : 6*job + variant + 5 (bornes 5..19).
// ⚠ CE N'EST PAS un « lookPresetId » : +216 est un ID D'OBJET, résolu dans la DB items
// par MobDb_GetEntry(mITEM) (EA 0x5274A3), au même titre que les 8 autres slots
// d'équipement (+0x78, +0x88, +0xB8, +0xE8, +0xF8, +0x108, +0x118, +0x128).
int32_t ResolveStartingWeaponItemId(int32_t job, int32_t variant) {
    static constexpr int32_t kTable[3][3] = {
        { 5,  6,  7},
        {11, 12, 13},
        {17, 18, 19},
    };
    if (job < 0 || job > 2 || variant < 0 || variant > 2) return 0; // non atteint normalement
    return kTable[job][variant];
}

// Remise à zéro de l'aperçu — motif `motion=0 ; animState=1 ; timer=0.0` répété tel quel
// aux EA 0x51c356/0x51c363/0x51c372 (Init), 0x51c6c0/CD/DC (anti-rejeu),
// 0x51c962/6F/7E, 0x51ca0f/1C/2B, 0x51cac5/D2/E1, 0x51cbc3/D0/DF (erreurs récupérables).
void ResetPreviewToIdle(CharSelectState& s) {
    s.previewMotionIndex = 0;                    // this[15717]
    s.previewMotion      = PreviewMotion::Idle;  // this[15718] = 1
    s.previewElapsed     = 0.0f;                 // this[15719] = 0.0
}

// PcModel_ResolveSlotAndApply 0x4E5A00 -> nombre de frames de l'animation.
// Les 4 derniers arguments sont CONSTANTS aux 6 sites d'appel : (…, 1, 0, 0, 1).
//
// ⚠ ZÉRO EST UNE VALEUR DE RETOUR LÉGITIME DU BINAIRE — NE PAS LA FILTRER.
// Chaîne complète : PcModel_ResolveSlotAndApply 0x4E5A00 = `Motion_GetFrameCount(
// PcModel_ResolveEquipSlot(...), a9)` (EA 0x4e5a2b/0x4e5a37), et Motion_GetFrameCount
// 0x4D7830 s'écrit :
//     if (!*(this+152) && !Motion_Load(this, a2)) return 0;   // EA 0x4d784b -> 0x4d7854
//     *(this+108) = g_GameTimeSec; return *(this+140);         // EA 0x4d7861/0x4d786d
// => sur motion ABSENTE/illisible, le binaire renvoie littéralement 0, et le flux amont
// s'en accommode tel quel : minuteur Idle `elapsed >= 0 -> elapsed -= 0` (boucle qui ne
// progresse plus, EA 0x51c5bb) et branche Entering `elapsed < 0` faux -> tir immédiat avec
// `elapsed = 0 - 1.0 = -1.0` (EA 0x51c6ae). Un repli à 30.0f ici ÉCRASERAIT une valeur
// PROUVÉE du binaire par une constante SANS ancre (kDefaultEnterPreviewFrames n'est qu'un
// artefact de portage) : ce serait une régression de fidélité, pas un correctif.
// D'où le test de PRÉSENCE du hook (et non de sa valeur) : le repli n'existe que pour les
// hôtes qui n'implémentent PAS du tout la résolution de motion.
// 🔴 RÉSIDUEL HORS PÉRIMÈTRE [ancre 0x4d7854] : UI/LoginScene.cpp::GetMotionFrameCount
// renvoie 0 pour DEUX raisons distinctes — « motion absente » (fidèle, miroir de 0x4d7854)
// ET « MotionCache non construit » (dégradation propre au portage, hors modèle du binaire).
// Les deux sont indiscernables ici. La correction est de CÂBLER le cache (défaut
// SceneManager/renderer), surtout pas de filtrer le 0 dans cette fonction.
float MotionFrameCount(const CharSelectHost& host, int32_t modelRace, int32_t modelGender,
                       int32_t motion, PreviewMotion animState, int32_t slotIndex) {
    if (host.GetMotionFrameCount) {
        return static_cast<float>(host.GetMotionFrameCount(
            modelRace, modelGender, motion, static_cast<int32_t>(animState)));
    }
    // Repli historique (UI/LoginScene.cpp:1807 assigne encore ce hook, à nullptr).
    if (host.GetEnterPreviewDurationFrames) return host.GetEnterPreviewDurationFrames(slotIndex);
    return kDefaultEnterPreviewFrames;
}

// Durée de l'animation de l'aperçu de la LISTE : arguments (rec+40 /*race*/,
// rec+44 /*genre*/) — EA 0x51c555 (`dword_16693A8[2522*slot]`, `dword_16693AC[2522*slot]`).
// ⚠ +40, PAS +36 : cf. la preuve à trois témoins dans CharSlotInfo::race (.h).
float ListMotionFrameCount(const CharSelectState& s, const CharSelectHost& host) {
    const CharSlotInfo& rec = s.slots[static_cast<size_t>(s.selectedSlot)];
    return MotionFrameCount(host, rec.race, rec.faction, s.previewMotionIndex,
                            s.previewMotion, s.selectedSlot);
}

// Durée de l'animation de l'aperçu de CRÉATION : arguments (dword_16709DC /*+36*/,
// dword_16709E4 /*+44*/) — EA 0x51cd7a. La branche création lit bien +36 (cohérent avec
// `mov edx,[ecx+24h]` @0x527051 de Char_RenderModel).
float CreateMotionFrameCount(const CharSelectState& s, const CharSelectHost& host) {
    return MotionFrameCount(host, s.createForm.job, s.createForm.faction,
                            s.previewMotionIndex, s.previewMotion, -1);
}

// Recalcule la sélection par défaut (EA 0x51c299 puis boucle 0x51c2ca-0x51c34b) :
//   this[15715] = -1 ;
//   for (i=0;i<3;++i)
//     if (Crt_Strcmp(name_i, "")) {                       // occupé
//       if (this[15715] == -1) this[15715] = i;
//       else if (dword_16693B8[2522*i] > dword_16693B8[2522*this[15715]]) this[15715] = i;
//     }
// Comparaison STRICTEMENT supérieure (`jle` EA 0x51c343) => à égalité, le PREMIER gagne.
void RecomputeDefaultSelection(CharSelectState& s) {
    s.selectedSlot = -1; // EA 0x51c299
    for (int32_t i = 0; i < kMaxCharSlots; ++i) {
        const CharSlotInfo& cur = s.slots[static_cast<size_t>(i)];
        if (!cur.occupied) continue;
        if (s.selectedSlot == -1) {
            s.selectedSlot = i; // EA 0x51c317
        } else if (cur.power > s.slots[static_cast<size_t>(s.selectedSlot)].power) {
            s.selectedSlot = i; // EA 0x51c34b
        }
    }
}

// this[15705] (+0xF564) — MIROIR EN ÉCRITURE SEULE (champ mort, cf. .h). ⚠ NE JAMAIS LIRE.
//
// POLARITÉ RE-DÉRIVÉE AU DÉSASSEMBLAGE (l'ancienne version de cette fonction calculait
// l'INVERSE et le champ s'appelait `allSlotsFull`). Boucle EA 0x51bec4-0x51bf0a :
//   0x51bec4  mov [ebp+var_10], 0                       ; i = 0
//   0x51bed6  cmp [ebp+var_10], 3 ; jge short loc_51BF01 ; i >= 3 -> sortie
//   0x51bef1  call Crt_Strcmp(&unk_1669394 + 0x2768*i, "")
//   0x51bef9  test eax, eax
//   0x51befb  jz short loc_51BEFF  -> 0x51beff jmp short loc_51BECD  ; nom VIDE  -> CONTINUE
//   0x51befd  jmp short loc_51BF01                                   ; nom PLEIN -> SORT
//   0x51bf01  cmp [ebp+var_10], 3 ; 0x51bf05 jl short loc_51BF14
//   0x51bf0a  mov dword ptr [eax+0F564h], 1
// => on avance sur les slots VIDES, on sort au premier OCCUPÉ, et le drapeau ne passe à 1
// que si LES 3 SLOTS SONT VIDES. D'où le renommage allSlotsFull -> allSlotsEmpty (.h).
// CONTRE-ÉPREUVE : la boucle « slot libre ? » du bouton CRÉER (EA 0x52524c-0x525289) a la
// polarité INVERSE (`jnz short loc_525287` EA 0x525283 = continue sur les slots OCCUPÉS) —
// c'est elle que modélise FindFirstFreeSlot(), et elle seule.
void RecomputeAllSlotsEmptyMirror(CharSelectState& s) {
    int32_t i = 0;
    for (; i < kMaxCharSlots && !s.slots[static_cast<size_t>(i)].occupied; ++i) {} // EA 0x51befb
    if (i >= kMaxCharSlots) s.allSlotsEmpty = true; // EA 0x51bf01/0x51bf05 -> 0x51bf0a
}

// this[15602] (+0xF3C8) = NOMBRE d'entrées de la liste de restauration — le SEUL champ lu
// de ce bloc (EA 0x5242ac). Formule EXACTE (EA 0x51c08d-0x51c153) :
//   if (g_ServerModeFlag)  count = (this[15374]==40) ? 1 : 7;   // EA 0x51c13f/0x51c144/0x51c153
//   else                   count = (this[15374]==40) ? 3 : 5;   // EA 0x51c09d/0x51c0a2/0x51c0b1
// La table this[15603..15611] écrite juste après n'est JAMAIS lue -> non modélisée.
void RecomputeRestoreListCount(CharSelectState& s, const CharSelectHost& host) {
    const bool    serverMode  = host.IsServerModeFlag && host.IsServerModeFlag();
    const int32_t serverIndex = host.GetServerIndex ? host.GetServerIndex() : 0;
    if (serverMode) s.restoreListCount = (serverIndex == 40) ? 1 : 7;
    else            s.restoreListCount = (serverIndex == 40) ? 3 : 5;
}

// Assistant PIN — branche `if (dword_16692A4)` EA 0x51beb5. Cf. [A12] dans le .h.
// ⚠ La permutation du pavé CONSOMME LE FLUX PRNG PARTAGÉ (10 tirages minimum, davantage
// par rejet) : elle doit être reproduite pour que le tirage du fond (EA 0x51c247) qui la
// SUIT tombe sur la même valeur que le binaire.
void InitPinWizard(CharSelectState& s, const CharSelectHost& host) {
    s.pin.panelOpen = true;   // this[15375] = 1   EA 0x51bf1c
    s.allSlotsEmpty = false;  // this[15705] = 0   EA 0x51bf29 (miroir mort)

    // `Crt_Strcmp(&unk_16692A8, "")` EA 0x51bf3d : PIN stocké non vide -> 2 (VÉRIFIER),
    // vide -> 1 (DÉFINIR).
    const bool hasStoredPin = host.HasStoredSecondaryPassword && host.HasStoredSecondaryPassword();
    s.pin.mode = hasStoredPin ? 2 : 1; // EA 0x51bf68 / 0x51bf4c
    s.pin.step = 0;                     // this[15377] = 0  EA 0x51bf59
    // TODO [ancre 0x51bf82] : this[15378..15380] = 0 (3 × Crt_StringInit EA 0x51bfb5/
    // 0x51bfcc/0x51bfe2, les 3 saisies char[5]) — non modélisés.

    for (auto& k : s.pin.keypad) k = -1;                 // EA 0x51c008
    for (int32_t i = 0; i < 10; ++i) {                    // EA 0x51c015
        int32_t v;
        do { v = net::DefaultRng().NextMod(10); }         // EA 0x51c043
        while (s.pin.keypad[static_cast<size_t>(v)] != -1); // EA 0x51c054
        s.pin.keypad[static_cast<size_t>(v)] = i;         // EA 0x51c05f
    }
    // TODO [ancre 0x51c06f] : this[15395] = 0 — non modélisé.
}

int32_t FindFirstFreeSlot(const CharSelectState& s) {
    for (int32_t i = 0; i < kMaxCharSlots; ++i) {
        if (!s.slots[static_cast<size_t>(i)].occupied) return i;
    }
    return -1;
}

// Erreurs RÉCUPÉRABLES : `g_MorphInProgress = 0` PUIS remise de l'aperçu à Idle.
// EA 0x51c955-0x51c97e, 0x51ca02-0x51ca2b, 0x51cab8-0x51cae1, 0x51cbb6-0x51cbdf.
// L'ordre importe : sans la remise à 0 du global, la tentative suivante retomberait sur
// la branche anti-rejeu (EA 0x51c6bb) et n'émettrait plus jamais rien.
void RecoverToIdle(CharSelectState& s) {
    net::g_MorphInProgress = 0; // 0x1675A88
    ResetPreviewToIdle(s);
}

// Séquence réseau « Entrer en jeu » — le BLOC EA 0x51c707-0x51cd01, dans l'ORDRE EXACT.
// Déclenchée UNIQUEMENT par le minuteur d'aperçu (jamais par le clic), et UNIQUEMENT si
// l'anti-rejeu g_MorphInProgress laisse passer (garde appliquée par l'appelant).
void FireEnterSequence(CharSelectState& s, const CharSelectHost& host) {
    const int32_t slot = s.selectedSlot;
    const CharSlotInfo& rec = s.slots[static_cast<size_t>(slot)];

    // (1) EA 0x51c707 — Crt_Memcpy(g_SelfCharInvBlock 0x1673170,
    //                              &unk_1669380 + 10088*slot, 0x2768u).
    // Pose À LA FOIS le bloc d'inventaire ET g_LocalElement (= bloc+0x24 = fiche[+36]),
    // qui part ensuite aux octets [137..140] du handshake op11 (EA 0x462d5d).
    // ORDRE PROUVÉ : 0x51c707 ≺ 0x51c81d (op22) ≺ 0x51c850 ≺ op11 — d'où l'appel EN TÊTE.
    if (host.PublishSelfFromSlot) host.PublishSelfFromSlot(slot);

    // (2) EA 0x51c70f — g_MorphInProgress = 1. C'est CE flag qui rend la séquence
    // non-rejouable : à la frame suivante, le minuteur est toujours >= durée, mais la
    // garde EA 0x51c6bb aiguille alors vers la remise à Idle au lieu de re-tirer.
    net::g_MorphInProgress = 1; // 0x1675A88

    // (3) EA 0x51c719-0x51c737 — dword_1675A8C=1, dword_1675A90=0, dword_1675A94=0,
    //     g_SelfMorphNpcId (0x1675A98) = 0.
    // TODO CÂBLAGE [ancres 0x51c719/0x51c723/0x51c72d/0x51c737] : ces 4 globals de l'état
    // « morph/self » ne sont pas réifiés dans ClientSource (seul g_MorphInProgress l'est).
    // Aucun consommateur C++ ne les lit à ce jour -> non posés ici plutôt qu'inventés.

    // (4) EA 0x51c756 — g_TargetZoneId (0x1675A9C) = dword_166A8DC[2522*slot] (fiche +5468).
    // PRÉ-SEED : Net_ReqEnterCharInfo reçoit &g_TargetZoneId comme out-param et ne l'écrit
    // QUE si code==0 (garde `if (!v19)` EA 0x52b2b7). Donc sur échec, le global garde la
    // valeur locale. Sans effet sur la transition (qui n'a lieu que sur code 0, où le
    // serveur a écrasé la valeur), d'où sa modélisation comme simple valeur de départ.
    int32_t targetZoneId = rec.localZoneId;

    // (5) EA 0x51c765-0x51c7d4 — Crt_Memset(&dword_1675AA0, 0, 0x48) puis position de
    // spawn aux offsets +0x0C/+0x10/+0x14 du bloc « struct72 ».
    // Ce bloc est déjà modélisé et consommé côté Net (net::BuildEnterWorldTail72,
    // Net/CharSelectPackets.h) à partir de g_World.self.spawnX/Y/Z, eux-mêmes peuplés
    // depuis slots[enterWorldSlot].localPosX/Y/Z par UI/LoginScene — rien à poser ici.

    // (6) EA 0x51c7ed/0x51c7f9 — flt_1675AC4 = (float)(Rng_Next() % 360) ; flt_1675AC8 =
    // flt_1675AC4 (MÊME valeur dupliquée, pas un 2e angle).
    // ⚠ POSITION DU TIRAGE : ICI, AVANT Net_ReqEnterCharInfo (EA 0x51c81d) — donc avant
    // les 4 tirages de nonce de l'op22 et les 4 de l'op11. Tirer cet angle APRÈS la
    // séquence réseau (ce que faisait UI/LoginScene.cpp:1194) donne une AUTRE valeur :
    // le flux PRNG est partagé et son ordre est observable.
    s.enterWorldSpawnRotationDeg = static_cast<float>(net::DefaultRng().NextMod(360));

    // (7) EA 0x51c81d — Net_ReqEnterCharInfo(slot, &domainId, &port, &g_TargetZoneId, &code)
    EnterCharInfoResult info{};
    info.zoneId = targetZoneId; // pré-seed (4) — écrasé par le serveur si code==0
    if (host.RequestEnterCharInfo) info = host.RequestEnterCharInfo(slot);

    switch (info.resultCode) { // EA 0x51c83c
        case 0: {
            // EA 0x51c850 Net_SelectServerDomain + EA 0x51c866 Net_ConnectGameServer
            const int32_t connectResult = host.ConnectToGameServer
                ? host.ConnectToGameServer(info.domainId, info.gamePort)
                : 101;
            switch (connectResult) { // EA 0x51c87e
                case 0:
                    // EA 0x51c888/0x51c891/0x51c89b — this[0]=5, this[1]=0, this[2]=0.
                    s.pendingTransition = CharSelectTransition::EnterWorld;
                    s.enterWorldZoneId  = info.zoneId; // AUTHENTIQUE (serveur)
                    s.enterWorldSlot    = slot;
                    s.subState          = CharSelectSubState::Init; // this[1]=0 EA 0x51c891
                    s.frameCounter      = 0;                        // this[2]=0 EA 0x51c89b
                    return;
                case 1: Notice(host, kStrConnectFail59, NoticeType::Disconnect); Lock(s); return;
                case 2: Notice(host, kStrConnectFail60, NoticeType::Disconnect); Lock(s); return;
                case 3:
                case 4:
                case 5: {
                    // Notice de TYPE 1 (simple fermeture) puis Net_ReqCancelEnter.
                    const int32_t connStr = (connectResult == 3) ? kStrConnectFail61
                                          : (connectResult == 4) ? kStrConnectFail62
                                                                 : kStrConnectFail63;
                    Notice(host, connStr, NoticeType::Close);
                    const int32_t cancelResult = host.CancelEnter ? host.CancelEnter() : 0;
                    if (cancelResult != 0) { // EA 0x51c94b / 0x51c9f8 / 0x51caab
                        if (cancelResult == 101) {
                            // LABEL_92 EA 0x51cae9-0x51cb14 : notice 20 type 2 + Locked.
                            Notice(host, kStrSessionExpired, NoticeType::Disconnect);
                            Lock(s);
                        }
                        // ÉCART D'ORIGINE REPRODUIT TEL QUEL : cancelResult != 0 && != 101
                        // -> AUCUN changement d'état (le binaire ne fait littéralement
                        // rien). L'aperçu reste figé en Entering et g_MorphInProgress
                        // reste à 1 => la garde anti-rejeu remettra l'aperçu à Idle à la
                        // frame suivante. Ce n'est pas un blocage : c'est la conséquence
                        // exacte du code d'origine.
                    } else {
                        RecoverToIdle(s); // EA 0x51c955-0x51c97e (et analogues)
                    }
                    return;
                }
                case 6: Notice(host, kStrConnectFail64, NoticeType::Disconnect); Lock(s); return;
                case 7: Notice(host, kStrConnectFail65, NoticeType::Disconnect); Lock(s); return;
                default: return; // `default: return;` — no-op fidèle
            }
        }
        case 1:
            // SEUL échec DOUX de toute la séquence : notice de TYPE 1, retour Idle, on
            // peut ré-essayer. EA 0x51cba4-0x51cbdf.
            Notice(host, kStrEnterInfoFail55, NoticeType::Close);
            RecoverToIdle(s);
            return;
        case 2:   Notice(host, kStrEnterInfoFail1347, NoticeType::Disconnect); Lock(s); return;
        case 3:   Notice(host, kStrEnterInfoFail1348, NoticeType::Disconnect); Lock(s); return;
        case 4:   Notice(host, kStrEnterInfoFail229,  NoticeType::Disconnect); Lock(s); return;
        case 101: Notice(host, kStrEnterInfoFail56,   NoticeType::Disconnect); Lock(s); return;
        case 102: Notice(host, kStrEnterInfoFail57,   NoticeType::Disconnect); Lock(s); return;
        default: return; // `default: return;` — no-op fidèle
    }
}

// Branche « écran LISTE » du sous-état Actif — EA 0x51c488-0x51cd01.
//   if (this[15715] == -1) rien du tout           (garde EA 0x51c4aa)
//   this[15718]==1 (Idle)     -> minuteur qui BOUCLE (EA 0x51c4e7-0x51c5bb)
//   this[15718]==3 (Entering) -> minuteur puis, à complétion, clamp + anti-rejeu + séquence
void UpdateListScreen(CharSelectState& s, const CharSelectHost& host, float dt) {
    if (s.selectedSlot == -1) return;                     // EA 0x51c4aa
    if (s.selectedSlot < 0 || s.selectedSlot >= kMaxCharSlots) return; // garde de portage

    if (s.previewMotion == PreviewMotion::Idle) {         // EA 0x51c4c1
        s.previewElapsed += dt * 30.0f;                   // EA 0x51c4e7
        const float dur = ListMotionFrameCount(s, host);  // EA 0x51c555
        // BOUCLE : soustraction, pas de clamp (EA 0x51c5bb). Le binaire ré-appelle
        // PcModel_ResolveSlotAndApply une 2e fois pour la soustraction — même valeur.
        if (s.previewElapsed >= dur) s.previewElapsed -= dur;
        return;
    }

    if (s.previewMotion != PreviewMotion::Entering) return; // EA 0x51c4c7 (`else if (v18==3)`)

    s.previewElapsed += dt * 30.0f;                        // EA 0x51c5db
    const float dur = ListMotionFrameCount(s, host);       // EA 0x51c649
    if (s.previewElapsed < dur) return;

    // CLAMP à (durée - 1) — EA 0x51c6ae. Fait AVANT le test d'anti-rejeu.
    s.previewElapsed = dur - 1.0f;

    // ANTI-REJEU — EA 0x51c6bb (`cmp g_MorphInProgress, 1`). Si un morph est déjà en
    // cours, on NE relance PAS : on remet l'aperçu à Idle (EA 0x51c6c0/CD/DC).
    // Conséquences fidèles : (a) après un tir, la frame suivante retombe ici et remet
    // proprement l'aperçu à Idle ; (b) revenir en CharSelect alors que le global est
    // resté à 1 rend le bouton ENTRER inopérant — c'est le comportement du binaire.
    if (net::g_MorphInProgress == 1) {
        ResetPreviewToIdle(s);
        return;
    }

    FireEnterSequence(s, host); // EA 0x51c707-0x51cd01
}

// Branche « écran CRÉATION » du sous-état Actif — EA 0x51cd25-0x51ce09.
// Le minuteur BOUCLE (aucune séquence réseau ici) puis les deux bascules font tourner
// l'aperçu. ⚠ La rotation n'existe QUE sur cet écran : la liste n'en a aucune.
void UpdateCreateScreen(CharSelectState& s, const CharSelectHost& host, float dt) {
    s.previewElapsed += dt * 30.0f;                        // EA 0x51cd25
    const float dur = CreateMotionFrameCount(s, host);     // EA 0x51cd7a
    if (s.previewElapsed >= dur) s.previewElapsed -= dur;  // BOUCLE, EA 0x51cdc7

    // this[15724] (+0xF5B0) = YAW. `+= 3.0` si this[15] (EA 0x51cdd0/0x51cde8),
    // `-= 3.0` si this[16] (EA 0x51cdf1/0x51ce09). Les DEUX tests sont indépendants
    // (pas de `else`) : les deux latches armés s'annulent exactement.
    if (host.IsRotateLeftLatched  && host.IsRotateLeftLatched())  s.previewRot[1] += 3.0f;
    if (host.IsRotateRightLatched && host.IsRotateRightLatched()) s.previewRot[1] -= 3.0f;
}

// Bloc d'initialisation complet — EA 0x51bded-0x51c3c7, dans l'ordre du binaire.
void RunInitBlock(CharSelectState& s, const CharSelectHost& host) {
    // (1) EA 0x51bded-0x51be65 — caméra : eye (0,5,-28) / target (0,10,0), écrite sur
    // les DEUX singletons (g_GfxRenderer 0x800130..0x800144 PUIS g_GxdRenderer
    // 0x18C51C0..0x18C51D4, copie identique EA 0x51be2c-0x51be65).
    // Hors périmètre de ce module (Gfx/) -> posée par UI/LoginScene (TODO A14 du front P2).

    // (2) EA 0x51be72 — Util_SetClampedU8Field(dword_8E714C, 0).
    // TODO [ancre 0x51be72] : état UI global non identifié, non modélisé.

    // (3) EA 0x51be7e — UI_FocusEditBox(&g_UIEditBoxMgr, 0) : rend le focus à la fenêtre
    // parente (index 0 = aucune saisie active).
    if (host.FocusEditBox) host.FocusEditBox(0);

    // (4) EA 0x51be83-0x51bea4 — `for (i=0;i<150;++i) this[i+3] = 0` : les 150 latches
    // this[3..152] (+12..+608). ⚠ 150, PAS 10 : OnMouseDown arme jusqu'à this[92].
    if (host.ClearAllButtonLatches) host.ClearAllButtonLatches();

    // (5) EA 0x51beb5 — `if (dword_16692A4)` : assistant PIN OU flux standard (EXCLUSIFS).
    // ⚠ Les deux branches ne consomment PAS le même nombre de tirages PRNG : la branche PIN
    // permute le pavé (>= 10 tirages), la branche standard aucun. Le tirage du fond
    // (EA 0x51c247) qui les suit en dépend.
    // TODO CÂBLAGE [ancre 0x51beb5] : tant que host.IsSecondaryPasswordRequired est nullptr,
    // le flux standard est TOUJOURS pris — correct pour un compte sans mot de passe
    // secondaire, FAUX pour un compte qui en a un (cf. [A12] dans le .h).
    if (host.IsSecondaryPasswordRequired && host.IsSecondaryPasswordRequired()) {
        InitPinWizard(s, host);          // EA 0x51bf1c-0x51c06f
    } else {
        s.pin.panelOpen = false;         // this[15375] = 0   EA 0x51beba
        RecomputeAllSlotsEmptyMirror(s); // this[15705]       EA 0x51bec4-0x51bf0a
    }

    // (6) EA 0x51c07c — this[15396] = 0.
    // TODO [ancre 0x51c07c] : drapeau du panneau Entrepôt, non modélisé.

    // (7) EA 0x51c08d-0x51c1c8 — this[15602] (compte, LU) + this[15603..15611] (table
    // MORTE, non modélisée).
    RecomputeRestoreListCount(s, host);

    // (8) EA 0x51c1d5-0x51c230 — champs divers remis à leur valeur d'init.
    // TODO [ancres 0x51c1d5/0x51c1ef/0x51c1fc/0x51c209/0x51c216] : this[15703] (panneau
    // « sélection rapide de classe »), this[15706] (Renommer), this[15707]/this[15708]/
    // this[15709] (liste étendue) — sous-systèmes non portés.
    s.restoreListIndex        = -1;    // this[15704] = -1  EA 0x51c1e2
    s.deleteByNamePanelOpen   = false; // this[15711] = 0   EA 0x51c223
    s.deleteByNameListFlag    = 0;     // this[15712] = 0   EA 0x51c230

    // (9) EA 0x51c247-0x51c27f — fond plein écran : `v20 = Rng_Next() % 3` puis
    // 2383 / 2384 / 2385. TIRÉ À CHAQUE ENTRÉE EN INIT (le bloc est immédiatement suivi
    // de this[15714]=1 EA 0x51c28c), pas une seule fois au démarrage de la scène.
    // ⚠ Le `default` (EA 0x51c289) N'ÉCRIT RIEN — donc à valeur inattendue, this[15713]
    // GARDE sa valeur précédente. Sans effet ici car Rng_Next()%3 ∈ {0,1,2}, mais on
    // reproduit la structure : aucun `else` fourre-tout.
    const int32_t bg = net::DefaultRng().NextMod(kCharBgSlotCount); // EA 0x51c247
    if (bg == 0)      s.backgroundSlot = kCharBgSlotFirst;       // 2383, EA 0x51c261
    else if (bg == 1) s.backgroundSlot = kCharBgSlotFirst + 1;   // 2384, EA 0x51c270
    else if (bg == 2) s.backgroundSlot = kCharBgSlotFirst + 2;   // 2385, EA 0x51c27f

    // (10) EA 0x51c28c/0x51c299 — écran Liste, aucune sélection.
    s.screen       = CharSelectScreen::List; // this[15714] = 1
    s.selectedSlot = -1;                     // this[15715] = -1

    // (11) EA 0x51c2a6/0x51c2b3/0x51c2c0 — this[15398]=0, this[15399]=0, this[15401]=-1.
    // TODO [ancres 0x51c2a6/0x51c2b3/0x51c2c0] : champs du panneau Entrepôt, non modélisés.

    // (12) EA 0x51c2ca-0x51c34b — sélection par défaut.
    RecomputeDefaultSelection(s);

    // (13) EA 0x51c356-0x51c3b4 — aperçu 3D entièrement remis à zéro (motion, animState=1,
    // timer, position vec3, rotation vec3).
    ResetPreviewToIdle(s);
    s.previewPos[0] = s.previewPos[1] = s.previewPos[2] = 0.0f; // EA 0x51c37d/88/93
    s.previewRot[0] = s.previewRot[1] = s.previewRot[2] = 0.0f; // EA 0x51c39e/a9/b4

    // (14) EA 0x51c3bd/0x51c3c7 — passage en Actif.
    s.subState     = CharSelectSubState::Active;
    s.frameCounter = 0;
}

} // namespace

CharSelectTransition UpdateCharSelect(CharSelectState& state, const CharSelectHost& host, float dt) {
    // --- Transitions armées HORS de cette fonction, à remonter EN TÊTE ---
    // Le binaire écrit `this[0]` DIRECTEMENT depuis des handlers hors-tick (bouton RETOUR
    // EA 0x525A51 dans OnMouseUp ; clic OK du NoticeDlg mode 2 EA 0x5C04E4, qui n'est même
    // pas dans la scène) : la bascule de scène y est IMMÉDIATE, aucun tick de CharSelect ne
    // s'intercale. Comme ces handlers posent AUSSI this[1]=0 (EA 0x525A5D / 0x5C04EE), une
    // consommation faite plus bas serait avalée par le `case Init` — d'où ce test en tête,
    // avant tout traitement de sous-état.
    if (state.pendingTransition != CharSelectTransition::None) {
        const CharSelectTransition t = state.pendingTransition;
        state.pendingTransition = CharSelectTransition::None;
        return t;
    }

    // EA 0x51bdac — `v21 = this[1]` : SEULS les sous-états 0 et 1 sont traités.
    // Le sous-état 2 (Verrouillé) ne matche AUCUN case => Update ne fait RIEN.
    switch (state.subState) {
    case CharSelectSubState::Init: {
        // EA 0x51bde4 — `else if (++this[2] >= 0x1Eu)`. Pendant ces 30 frames, le RENDU
        // ne dessine rien (garde EA 0x51D238) : 1 seconde d'écran noir, fidèle.
        ++state.frameCounter;
        if (state.frameCounter < kInitWaitFrames) return CharSelectTransition::None;

        // Les données de personnages sont relues à CHAQUE Init (le binaire lit
        // directement unk_1669394/16693A8/16693B8/166A8DC.. à chaque passage).
        if (host.LoadCharacterSlots) host.LoadCharacterSlots(state.slots);
        RunInitBlock(state, host);
        return CharSelectTransition::None;
    }
    case CharSelectSubState::Active: {
        // --- DOUBLE BATTEMENT : les DEUX tests `% 30` lisent le MÊME this[2] déjà
        // incrémenté, donc ils tombent SUR LA MÊME FRAME. Ordre strict (EA 0x51c40b puis
        // 0x51c46e), et le dispatch d'écran ne vient qu'APRÈS.
        ++state.frameCounter; // EA 0x51c40b (`++*(this+2) % 0x1Eu`)

        // (1) Keep-alive de session — Net_AccountKeepAlive 0x5298F0 (op12).
        // ⚠ this[2] N'EST PAS remis à 0 par un battement réussi : il court librement et
        // le modulo re-déclenche toutes les 30 frames.
        if (state.frameCounter % kKeepAliveIntervalFrames == 0) {
            const int32_t keepAlive = host.AccountKeepAlive ? host.AccountKeepAlive() : 0;
            if (keepAlive == 101) {
                // EA 0x51c41d/0x51c42a/0x51c432/0x51c43c — notice 20 de TYPE 2 puis
                // Verrouillé. Le type 2 est ce qui permettra au clic OK du NoticeDlg de
                // ramener vers ServerSelect (cf. OnNoticeDlgMode2Ok).
                Notice(host, kStrSessionExpired, NoticeType::Disconnect);
                Lock(state);
                return CharSelectTransition::None;
            }
        }

        // (2) Battement anti-triche — MÊME frame, MÊME modulo (EA 0x51c46e).
        // `if (Ac_GameGuard_Heartbeat() != 1877) { g_QuitFlag = 1; return; }`
        // (`cmp eax, 755h` EA 0x51c469 -> `g_QuitFlag = 1` EA 0x51c470).
        // L'anticheat est hors périmètre : hook absent => battement réputé réussi.
        if (state.frameCounter % kKeepAliveIntervalFrames == 0) {
            const int32_t beat = host.GameGuardHeartbeat ? host.GameGuardHeartbeat()
                                                         : kGameGuardHeartbeatOk;
            if (beat != kGameGuardHeartbeatOk) {
                if (host.RequestQuit) host.RequestQuit(); // g_QuitFlag = 1
                return CharSelectTransition::None;         // pas de dispatch d'écran
            }
        }

        // (3) Dispatch d'écran — `v19 = this[15714]` EA 0x51c488.
        if (state.screen == CharSelectScreen::List)            UpdateListScreen(state, host, dt);
        else if (state.screen == CharSelectScreen::CreateForm) UpdateCreateScreen(state, host, dt);

        // EnterWorld armée pendant CE tick (FireEnterSequence, EA 0x51c888 `this[0]=5`) :
        // le binaire bascule de scène dans la foulée, donc on la remonte SANS attendre le
        // tick suivant.
        if (state.pendingTransition != CharSelectTransition::None) {
            const CharSelectTransition t = state.pendingTransition;
            state.pendingTransition = CharSelectTransition::None;
            return t;
        }
        return CharSelectTransition::None;
    }
    case CharSelectSubState::Locked:
    default:
        // MODAL GELÉ (pas un état mort) : Update inerte et souris inerte, mais le rendu
        // continue d'afficher l'image complète (garde EA 0x51D238 : seul le sous-état 0
        // saute le dessin). La SEULE sortie est le clic OK d'un NoticeDlg de mode 2, qui
        // arrive HORS SCÈNE -> OnNoticeDlgMode2Ok() ; sa transition est remontée par le
        // test en tête de cette fonction.
        return CharSelectTransition::None;
    }
}

void SelectCharacterSlot(CharSelectState& state, int32_t slotIndex) {
    if (state.subState != CharSelectSubState::Active) return; // gardes EA 0x520F4D/0x522E70
    if (state.screen != CharSelectScreen::List) return;
    if (state.previewMotion == PreviewMotion::Entering) return; // verrou universel
    if (slotIndex < 0 || slotIndex >= kMaxCharSlots) return;
    if (!state.slots[static_cast<size_t>(slotIndex)].occupied) return;
    if (slotIndex == state.selectedSlot) return;

    state.selectedSlot   = slotIndex;
    state.previewElapsed = 0.0f; // EA 0x5226b9
}

void OnCreateButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.screen != CharSelectScreen::List) return;
    if (state.previewMotion == PreviewMotion::Entering) return; // verrou EA 0x52523E

    // (1) EA 0x52524c-0x525289 — « reste-t-il un slot libre ? ». La boucle avance sur les
    // slots OCCUPÉS (`jnz short loc_525287` EA 0x525283) et sort au premier VIDE ; var_28==3
    // (EA 0x525289) = aucun libre -> notice 22 type 1 (EA 0x525294/0x5252a1) puis RETURN
    // (`jmp loc_526B7C` EA 0x5252ad). ⚠ Polarité INVERSE de la boucle 0x51bec4 (cf.
    // RecomputeAllSlotsEmptyMirror) : ne pas confondre les deux.
    if (FindFirstFreeSlot(state) == -1) {
        Notice(host, kStrCreateListFull, NoticeType::Close);
        return;
    }

    // (2) GARDE RÉGIONALE BLOQUANTE — EA 0x5252b2-0x5252eb, RE-DÉSASSEMBLÉE :
    //   0x5252b8  cmp dword ptr [eax+0F038h], 28h  ; this[15374] == 40 ?
    //   0x5252bf  jnz short loc_5252F0             ; != 40 -> on ouvre le formulaire
    //   0x5252c1  cmp ds:g_ServerModeFlag, 0       ; global 0x166918C
    //   0x5252c8  jnz short loc_5252F0             ; mode serveur -> on ouvre quand même
    //   0x5252cf  push 873h (=2163) ... 0x5252df push 1 ... 0x5252e6 UI_NoticeDlg_Open
    //   0x5252eb  jmp loc_526B7C                   ; RETURN
    // => blocage SSI (serverIndex == 40 && !g_ServerModeFlag).
    // ⚠ À NE PAS CONFONDRE avec la garde citée par l'ancien TODO de ce bloc
    // (EA 0x51dfa5 `== 60` / 0x51dfb8 `== 50`) : celle-là est dans Scene_CharSelectRender
    // 0x51CED0 et ne fait que MASQUER LE DESSIN du bouton — c'est une affaire de rendu
    // (UI/LoginScene), pas de flux. Les deux gardes coexistent et sont distinctes.
    // Repli des deux hooks (nullptr) : si=0, sm=false -> garde inerte, ce qui est
    // exactement le comportement du binaire à this[15374]==0.
    const int32_t serverIndex = host.GetServerIndex ? host.GetServerIndex() : 0;
    const bool    serverMode  = host.IsServerModeFlag && host.IsServerModeFlag();
    if (serverIndex == 40 && !serverMode) {
        Notice(host, kStrCreateRegionBlocked, NoticeType::Close);
        return;
    }

    // (3) EA 0x5252f7 — Util_SetClampedU8Field(dword_8E714C, 0), le MÊME appel que l'Init
    // (EA 0x51be72). TODO [ancre 0x5252f7] : état UI global non identifié, non modélisé.

    // (4) EA 0x525303 — UI_FocusEditBox(&g_UIEditBoxMgr, 3) (`push 3` EA 0x5252fc) : le
    // champ NOM du formulaire prend le focus clavier à l'ouverture.
    if (host.FocusEditBox) host.FocusEditBox(3);

    // (5) TODO [ancre 0x525314] : SetWindowTextA(dword_1668FCC, "") — vidage du WIDGET de
    // saisie du nom (le modèle, lui, est vidé en (7) par `CharCreateForm{}`). Aucun hook de
    // l'hôte n'expose ce SetWindowTextA (GetEditedName ne fait que LIRE) : on ne fabrique
    // pas un hook que personne n'assignerait.

    // (6) EA 0x52531a-0x525346 — `for (i=0;i<150;++i) this[i+3] = 0` (`cmp var_28, 96h`
    // EA 0x52532c ; `mov [ecx+eax*4+0Ch], 0` EA 0x52533e) : le bouton CRÉER purge les 150
    // latches de boutons, EXACTEMENT comme l'Init (EA 0x51be83-0x51bea4).
    if (host.ClearAllButtonLatches) host.ClearAllButtonLatches();

    // (7) EA 0x52534e-0x525396 — passage à l'écran de création puis fiche neuve, dans
    // l'ordre du binaire : screen=2 (0x52534e) ; variant=0 (0x52535e) ; nom="" (0x525368) ;
    // job=Rng_Next()%3 (0x52537c) ; genre=0 (0x525382) ; visage=0 (0x52538c) ;
    // cheveux=0 (0x525396).
    state.screen     = CharSelectScreen::CreateForm;
    state.createForm = CharCreateForm{};
    state.createForm.job = host.RandomInitialJob ? (host.RandomInitialJob() % 3) : 0;

    // (8) EA 0x5253a6-0x52541c — aperçu 3D ENTIÈREMENT remis à zéro, exactement comme le
    // bloc d'Init (EA 0x51c356-0x51c3b4) : motion, animState=1, minuteur, PUIS la position
    // vec3 et la rotation vec3 (6 `fldz`/`fstp` que ResetPreviewToIdle ne couvre pas).
    ResetPreviewToIdle(state);                                  // EA 0x5253a6/0x5253b6/0x5253c8
    state.previewPos[0] = state.previewPos[1] = state.previewPos[2] = 0.0f; // EA 0x5253d6/e4/f2
    state.previewRot[0] = state.previewRot[1] = state.previewRot[2] = 0.0f; // EA 0x525400/40e/41c
    // RETURN — `jmp loc_526B7C` EA 0x525422.
}

namespace {
// Les flèches du binaire ne « clampent » pas : elles SORTENT SANS RIEN FAIRE à la borne
// (`if (champ == 0) return` pour −, `if (champ == max) return` pour +). Effet identique à
// un clamp pour delta = ±1, mais on reproduit la forme : aucune écriture à la borne.
inline bool StepField(int32_t& field, int32_t delta, int32_t lo, int32_t hi) {
    const int32_t next = field + delta;
    if (next < lo || next > hi) return false;
    field = next;
    return true;
}
} // namespace

void SetCreateJob(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    // − : `if (dword_16709DC == 0) return;` EA 0x52609b ; `sub ecx,1` EA 0x5260b2.
    // + : `if (dword_16709DC == 2) return;` EA 0x526141 ; `add ecx,1` EA 0x526155.
    if (!StepField(state.createForm.job, delta, 0, 2)) return;

    // Le job réinitialise TOUTES ses sous-options — bloc identique sur les deux flèches
    // (− : EA 0x5260b8-0x5260ee ; + : EA 0x526158-0x526182 et suivantes) :
    state.createForm.faction   = 0;    // dword_16709E4 = 0  EA 0x5260b8
    state.createForm.face      = 0;    // dword_16709E8 = 0  EA 0x5260c2
    state.createForm.hairColor = 0;    // dword_16709EC = 0  EA 0x5260cc
    state.createForm.variant   = 0;    // this[15716]  = 0  EA 0x5260dc / 0x526182
    state.previewElapsed       = 0.0f; // this[15719]  = 0.0 EA 0x5260ee
}

void SetCreateFaction(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    StepField(state.createForm.faction, delta, 0, 1); // EA 0x5261F8 / 0x526280
}

void SetCreateFace(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    StepField(state.createForm.face, delta, 0, 6); // EA 0x526305 / 0x52636B
}

void SetCreateHairColor(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    StepField(state.createForm.hairColor, delta, 0, 2); // EA 0x5263D1 / 0x52643A
}

void SetCreateVariant(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    // this[15716] — gardes `cmp ...,0` EA 0x526490 (−) et `cmp ...,2` EA 0x52650B (+).
    StepField(state.createForm.variant, delta, 0, 2);
}

bool ConfirmCreateCharacter(CharSelectState& state, const CharSelectHost& host) {
    if (state.screen != CharSelectScreen::CreateForm) return false;

    // Ordre EXACT des 3 notices (EA 0x52658f-0x526623) : vide -> 38 ; caractères refusés
    // -> 39 ; mot banni -> 40. La borne dure à 12 caractères utiles fait partie de
    // ValidateNameCharset (échec de conversion sur buffer de 13 WCHAR).
    const std::string name = host.GetEditedName ? host.GetEditedName() : std::string{};
    if (name.empty()) {
        Notice(host, kStrCreateEmptyName, NoticeType::Close); // notice 38
        return false;
    }
    if (host.ValidateNameChars && !host.ValidateNameChars(name)) {
        Notice(host, kStrCreateInvalidChars, NoticeType::Close); // notice 39
        return false;
    }
    if (host.IsNameBanned && host.IsNameBanned(name)) {
        Notice(host, kStrCreateBannedWord, NoticeType::Close); // notice 40, EA 0x5265FC
        return false;
    }

    state.createForm.name = name;
    const int32_t slot = FindFirstFreeSlot(state);
    if (slot == -1) {
        Notice(host, kStrCreateListFull, NoticeType::Close);
        return false;
    }

    // fiche +216 = arme de départ = 6*job + variant + 5 (EA 0x52669A..0x52675B).
    const int32_t weaponItemId = ResolveStartingWeaponItemId(state.createForm.job,
                                                             state.createForm.variant);
    const int32_t code = host.CreateCharacter
        ? host.CreateCharacter(slot, state.createForm, weaponItemId)
        : 101;

    switch (code) {
        case 0:
            Notice(host, kStrCreateSuccess, NoticeType::Close);
            // Le miroir net::g_CharRecords est mis à jour par net::CreateCharacter depuis
            // l'écho op17 (EA 0x52a71e) ; le prochain Init relira les fiches.
            state.slots[static_cast<size_t>(slot)].occupied = true;
            state.screen       = CharSelectScreen::List;
            state.selectedSlot = slot;
            ResetPreviewToIdle(state);
            return true;
        case 1:    Notice(host, kStrCreateFail42,  NoticeType::Disconnect); Lock(state); return false;
        case 2:    Notice(host, kStrCreateFail43,  NoticeType::Close);      return false;
        case 3:    Notice(host, kStrCreateFail701, NoticeType::Close);      return false;
        case 0x65: Notice(host, kStrCreateFail44,  NoticeType::Disconnect); Lock(state); return false;
        case 0x66: Notice(host, kStrCreateFail45,  NoticeType::Disconnect); Lock(state); return false;
        default: return false; // no-op fidèle
    }
}

void CancelCreateForm(CharSelectState& state) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    state.screen = CharSelectScreen::List; // this[15715] (sélection) INCHANGÉ, fidèle
    ResetPreviewToIdle(state);
}

bool OnDeleteButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false; // verrou EA 0x525484
    // TODO fidélité [ancre 0x51E046] : le bouton n'est même pas DESSINÉ si
    // this[15374] == 60 — garde de rendu, hors périmètre de ce module.

    if (state.selectedSlot == -1) {
        Notice(host, kStrEnterNoSelection, NoticeType::Close); // notice 47, partagée
        return false;
    }

    if (host.ShowDeleteConfirm) host.ShowDeleteConfirm();
    return true;
}

void ConfirmDeleteCharacter(CharSelectState& state, const CharSelectHost& host) {
    if (state.selectedSlot == -1) return;
    const int32_t slot = state.selectedSlot;
    const int32_t code = host.DeleteCharacter ? host.DeleteCharacter(slot) : 101;

    switch (code) {
        case 0:
            Notice(host, kStrDeleteSuccess, NoticeType::Close);
            state.slots[static_cast<size_t>(slot)] = CharSlotInfo{};
            state.selectedSlot = -1;
            // ⚠ PAS de `allSlotsEmpty = ...` ici : ce serait une INVENTION. Le binaire ne
            // touche JAMAIS +0xF564 dans ce chemin — les 3 sites de OnMouseUp l'écrivent
            // à *1* (EA 0x5230be/0x52335e/0x5237e9) et le seul site à 0 est la branche PIN
            // (EA 0x51bf29). Le drapeau reste donc collé à sa dernière valeur, y compris
            // après une suppression qui libère un slot. C'est sans conséquence (0 lecture
            // prouvée) — et c'est précisément pourquoi il ne doit rien gater.
            return;
        case 1:    Notice(host, kStrDeleteFail51,   NoticeType::Disconnect); Lock(state); return;
        case 2:    Notice(host, kStrDeleteFail411,  NoticeType::Close);      return;
        case 3:    Notice(host, kStrDeleteFail48,   NoticeType::Close);      return;
        case 4:    Notice(host, kStrDeleteFail633,  NoticeType::Close);      return;
        case 5:    Notice(host, kStrDeleteFail2091, NoticeType::Close);      return;
        case 0x65: Notice(host, kStrDeleteFail52,   NoticeType::Disconnect); Lock(state); return;
        case 0x66: Notice(host, kStrDeleteFail53,   NoticeType::Disconnect); Lock(state); return;
        default: return; // no-op fidèle
    }
}

// ===========================================================================
// 🔴 PANNEAU « SUPPRESSION PAR SAISIE DU NOM » (opcode 24) — CHAÎNE MORTE
// ===========================================================================
// Ces trois fonctions sont un PORT FIDÈLE d'un code INATTEIGNABLE dans le binaire (preuve
// complète en tête de CharSelectFlow.h, [H1] : `var_434` invariant à 0 sur la fonction
// ENTIÈRE => 0x52601D, l'unique écriture de 1 dans +0xF57C, n'est jamais exécutée).
// Elles doivent RESTER SANS APPELANT — « une fonction morte dans le binaire reste morte
// en C++ ». Ne câbler AUCUN clic vers elles.
// ===========================================================================

void OpenDeleteByNamePanel(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.screen != CharSelectScreen::List) return;
    // La garde d'origine (`var_430`/`var_434`, EA 0x525f62/0x525f91) est ce qui rend ce
    // bloc inatteignable : var_434 == 0 est invariant => notice 2248 systématique.

    if (host.ClearDeleteByNameInput) host.ClearDeleteByNameInput(); // EA 0x525fe3
    if (host.FocusEditBox) host.FocusEditBox(19);                   // `push 13h` EA 0x525fcc

    state.deleteByNamePanelOpen = true; // +0xF57C = 1 (EA 0x52601d — JAMAIS EXÉCUTÉ)
    state.deleteByNameListFlag  = 1;    // +0xF580 = 1 (EA 0x52602d)
}

void CancelDeleteByNamePanel(CharSelectState& state) {
    state.deleteByNamePanelOpen = false; // +0xF57C = 0 (EA 0x524ff4)
    state.deleteByNameListFlag  = 0;     // +0xF580 = 0 (EA 0x525004)
}

void ConfirmDeleteCharByName(CharSelectState& state, const CharSelectHost& host) {
    const std::string name = host.GetDeleteByNameInput ? host.GetDeleteByNameInput()
                                                       : std::string{};
    if (name.empty()) {
        Notice(host, kStrDeleteByNameEmpty, NoticeType::Close); // 1463, SANS envoi (EA 0x52928c)
        return;
    }

    // slotEnc = *(_BYTE*)(this+62860) + 100 * *(_BYTE*)(this+62848) (EA 0x5292cd) :
    // selectedSlot (+0xF58C) et listFlag (+0xF580) lus en OCTET (donc -1 -> 255).
    const int32_t slotEnc = static_cast<uint8_t>(state.selectedSlot)
                          + 100 * static_cast<uint8_t>(state.deleteByNameListFlag);
    const int32_t code = host.VerifyCharName ? host.VerifyCharName(slotEnc, name) : 101;

    switch (code) { // EA 0x5292f5
        case 0:
            Notice(host, kStrDeleteByNameOk, NoticeType::Close); // 1464
            state.selectedSlot          = -1;    // EA 0x529348
            state.deleteByNamePanelOpen = false; // EA 0x52939e
            state.deleteByNameListFlag  = 0;     // EA 0x5293ae
            if (host.FocusEditBox) host.FocusEditBox(0); // EA 0x529365
            return;
        case 1: Notice(host, kStrDeleteByNameFail1, NoticeType::Close); return; // 1465
        case 2: Notice(host, kStrDeleteByNameFail2, NoticeType::Close); return; // 1468
        case 3: Notice(host, kStrDeleteByNameFail3, NoticeType::Close); return; // 1469
        case 4: Notice(host, kStrDeleteByNameFail4, NoticeType::Close); return; // 1466
        case 5: Notice(host, kStrDeleteByNameFail5, NoticeType::Close); return; // 1470
        case 101:
            Notice(host, kStrDeleteByNameFail101, NoticeType::Disconnect); // 703
            Lock(state); // EA 0x5294a2/0x5294af
            return;
        case 102:
            Notice(host, kStrDeleteByNameFail102, NoticeType::Disconnect); // 704
            Lock(state); // EA 0x5294df/0x5294ec
            return;
        default:
            // TODO [ancre 0x529503] : notice Crt_Vsnprintf("%s%d", StrTable005(2455), code)
            // — le hook ne transporte ni chaîne formatée ni code numérique.
            return;
    }
}

void ConfirmRestoreCharacter(CharSelectState& state, const CharSelectHost& host) {
    const int32_t code = host.RestoreCharacter
        ? host.RestoreCharacter(state.selectedSlot, state.restoreListIndex) // EA 0x5295f6
        : 101;

    switch (code) { // EA 0x529615
        case 0:
            Notice(host, kStrRestoreOk, NoticeType::Close); // 1271
            state.selectedSlot = -1;                        // EA 0x529662
            return;
        case 1:
            Notice(host, kStrRestoreSessLost1, NoticeType::Disconnect); // 51
            Lock(state); // EA 0x529692/0x52969c
            return;
        case 2: Notice(host, kStrRestoreFail1272, NoticeType::Close); return; // 1272
        case 5: Notice(host, kStrRestoreFail2091, NoticeType::Close); return; // 2091
        // Codes 11..15 : UI_NoticeDlg_Open(_, 1, corps, CAPTION=StrTable005(2546)) — le
        // 4e argument (titre) n'est pas transporté par les hooks ; seul le corps est rendu.
        case 11: Notice(host, kStrRestoreFail2541, NoticeType::Close); return;
        case 12: Notice(host, kStrRestoreFail2542, NoticeType::Close); return;
        case 13: Notice(host, kStrRestoreFail2545, NoticeType::Close); return;
        case 14: Notice(host, kStrRestoreFail2543, NoticeType::Close); return;
        case 15: Notice(host, kStrRestoreFail2544, NoticeType::Close); return;
        case 101:
            Notice(host, kStrRestoreSessLost101, NoticeType::Disconnect); // 52
            Lock(state); // EA 0x529807/0x529811
            return;
        case 102:
            Notice(host, kStrRestoreSessLost102, NoticeType::Disconnect); // 53
            Lock(state); // EA 0x52983b/0x529845
            return;
        default: return; // no-op fidèle
    }
}

// Bouton ENTRER — « Temps 1 » (EA 0x525062-0x5251F0). N'ÉMET AUCUN PAQUET : il ne fait
// qu'ARMER l'aperçu. La séquence réseau part au minuteur (UpdateListScreen).
bool OnEnterButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false; // verrou EA 0x5250A2

    if (state.selectedSlot == -1) {
        Notice(host, kStrEnterNoSelection, NoticeType::Close); // notice 47 type 1, EA 0x5250C4
        return false;
    }

    // `cmp g_GmAuthLevel, 1 ; jge` EA 0x5250E2 : un GM (niveau >= 1) SAUTE ENTIÈREMENT la
    // validation du nom — l'appel à Str_ValidateNameChars n'est même pas exécuté.
    const bool gmAuth = host.HasGmAuthLevel && host.HasGmAuthLevel();
    if (!gmAuth) {
        // EA 0x525109 — Str_ValidateNameChars(unk_1685740, &unk_1669394 + 10088*slot).
        const bool nameOk = !host.IsCharacterNameValid // nullptr => true (repli documenté)
                          || host.IsCharacterNameValid(state.selectedSlot);
        if (!nameOk) {
            Notice(host, kStrEnterInvalidName, NoticeType::Close); // notice 1856, EA 0x525117
            return false;
        }
    }

    // EA 0x525156/0x52515B/0x525163 — Weapon_ClassFromField112(g_EquipSnapshotScratch,
    // &unk_16693E8 + 10088*slot) -> classe 1..3, puis `shl eax,1` et
    // `this[15717] = 2*classe`. unk_16693E8 = fiche +104 (bloc d'équipement) ; la classe
    // est dérivée de l'id d'objet à +112 de ce bloc (= fiche +216, l'arme de départ).
    state.previewMotionIndex = host.GetEnterPreviewWeaponClass
        ? 2 * host.GetEnterPreviewWeaponClass(state.selectedSlot)
        : 0; // TODO [ancre 0x4CC870] : sans la DB items, le motion reste 0 (au lieu de 2/4/6)

    state.previewMotion  = PreviewMotion::Entering; // this[15718] = 3  EA 0x52516F
    state.previewElapsed = 0.0f;                    // this[15719] = 0.0 EA 0x525181

    // EA 0x5251E4/0x5251EB — PcSnd_ResolveEquipSlot(g_ModelMotionArray, rec+40, rec+44,
    // this[15717], 3, 1,0,0,0, 100, 1) puis Snd3D_PlayScaledVolume : le son d'arme de
    // l'entrée. C'est l'UNIQUE appel audio de tout Scene_CharSelectOnMouseUp.
    // Hors périmètre de ce module (Audio/) -> à jouer par UI/LoginScene sur retour true.
    return true;
}

// Bouton RETOUR — EA 0x525A33-0x525A6A. Destination = scène 2 (ServerSelect), PAS Login,
// et SANS aucune notice.
bool RequestBackToServerSelect(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false; // garde EA 0x525A33

    if (host.CloseConnection) host.CloseConnection(); // Net_CloseSocket EA 0x525A46
    state.pendingTransition = CharSelectTransition::ServerSelect; // this[0]=2 EA 0x525A51
    state.subState          = CharSelectSubState::Init;           // this[1]=0 EA 0x525A5D
    state.frameCounter      = 0;                                  // this[2]=0 EA 0x525A6A
    return true;
}

void OnQuitButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.previewMotion == PreviewMotion::Entering) return; // garde EA 0x525ABE
    if (host.CloseConnectionAndQuit) host.CloseConnectionAndQuit();
}

// Clic OK d'un NoticeDlg de MODE 2 — UI_NoticeDlg_OnLButtonUp 0x5C03F0, case dlg==2
// (EA 0x5C04C9). SEULE sortie de l'état Verrouillé, et elle arrive par une route HORS
// SCÈNE (les handlers souris de la scène sont gatés `== 1` et ne la verraient jamais) :
//   UI_RouteLButtonUp 0x5AD0F0 -> UI_NoticeDlg_OnLButtonUp 0x5C03F0 (xref unique 0x5AD164).
void OnNoticeDlgMode2Ok(CharSelectState& state, const CharSelectHost& host) {
    if (host.CloseConnection) host.CloseConnection();             // EA 0x5C04DF
    state.pendingTransition = CharSelectTransition::ServerSelect; // g_SceneMgr = 2 EA 0x5C04E4
    state.subState          = CharSelectSubState::Init;           // g_SceneSubState = 0 EA 0x5C04EE
    state.frameCounter      = 0;                                  // dword_1676188 = 0 EA 0x5C04F8
}

} // namespace ts2::game

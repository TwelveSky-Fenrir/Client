// Game/CharSelectFlow.h — flux d'état de l'écran de SÉLECTION DE PERSONNAGE (scène 4).
//
// Réécriture C++ fidèle du FLUX/DÉCISION (liste, sélection, création, suppression,
// entrée en jeu) de :
//   Scene_CharSelectUpdate      0x51BD90  (machine d'état — LE cœur de ce module)
//   Scene_CharSelectOnMouseDown 0x520F40  (latches de boutons)
//   Scene_CharSelectOnMouseUp   0x522E50  (validation clic + requêtes réseau)
//   Scene_CharSelectRender      0x51CED0  (pur D3D9 — porté par UI/LoginScene.cpp)
// Fonctions réseau associées : Net_CreateCharacter 0x52A4A0 (op17), Net_CharSlotAction
// 0x52A740 (op18), Net_ReqEnterCharInfo 0x52B070 (op22), Net_ReqCancelEnter 0x52B310
// (op23), Net_ReqVerifyCharName 0x52B4C0 (op24), Net_AccountKeepAlive 0x5298F0 (op12),
// Net_ConnectGameServer 0x462A70 (op11), Net_SelectServerDomain 0x53FE90.
//
// IDENTITÉ MÉMOIRE (prouvée) : le `this` de la scène EST le bloc de globals 0x1676180,
// donc this[N] ≡ 0x1676180 + 4*N. this[0]=id de scène, this[1]=sous-état, this[2]=
// compteur de frames.
//
// ===========================================================================
// MACHINE D'ÉTATS (this[1]) — ASYMÉTRIE DES 4 POINTS D'ENTRÉE
// ===========================================================================
//   Sous-état 0 (Init)   : Update compte 30 frames puis initialise tout (EA 0x51bde4 :
//     `++this[2] >= 0x1Eu`) -> this[1]=1 (EA 0x51c3bd), this[2]=0 (EA 0x51c3c7).
//     PENDANT ces 30 frames le RENDU NE DESSINE RIEN (garde EA 0x51D238 : End2D +
//     Present + return) = 1 seconde d'écran NOIR, et la souris est inerte.
//   Sous-état 1 (Actif)  : tout est actif (seul état où les handlers souris passent —
//     gardes `cmp [eax+4],1` EA 0x520F4D et 0x522E70).
//   Sous-état 2 (Verrouillé) : Update NE FAIT RIEN (EA 0x51bdac : seuls 0 et 1 sont
//     traités) et les DEUX handlers souris retournent immédiatement — MAIS le rendu
//     dessine TOUT (image figée). C'est un MODAL GELÉ, pas un état mort.
//     ÉCHAPPEMENT HORS SCÈNE (prouvé) : le NoticeDlg reçoit ses clics par une AUTRE
//     route que la scène — UI_RouteLButtonUp 0x5AD0F0 -> UI_NoticeDlg_OnLButtonUp
//     0x5C03F0 (xref unique EA 0x5AD164). Sur OK d'un NoticeDlg de MODE 2 (EA 0x5C04C9) :
//       Net_CloseSocket(&g_NetClient) 0x5C04DF ; g_SceneMgr=2 (ServerSelect) 0x5C04E4 ;
//       g_SceneSubState=0 0x5C04EE ; dword_1676188=0 0x5C04F8.
//     -> D'où NoticeType (ci-dessous) : le TYPE de la notice détermine si l'état
//        Verrouillé est un cul-de-sac (mode 1) ou une sortie vers ServerSelect (mode 2).
//        C'est UI/LoginScene qui doit router ce clic (cf. wiring TODO NOTICEDLG_MODE2).
//
// ===========================================================================
// DEUX CORRECTIONS DE DOCUMENTATION (les anciens commentaires de ce fichier étaient FAUX)
// ===========================================================================
// [H1] « Suppression par saisie du nom » (opcode 24) : ce N'EST PAS un « second
//   mécanisme de suppression à double confirmation » — la chaîne est ENTIÈREMENT MORTE.
//   PREUVE (re-établie sur la fonction ENTIÈRE [0x522E50, 0x526B90), pas sur un
//   sous-bloc) : le slot de pile `var_434` n'a que 3 références dans TOUTE la fonction —
//   la déclaration de frame (0x522E50), UNE écriture `mov [ebp+var_434], 0` (0x525DFA)
//   et UN `cmp [ebp+var_434], 0` (0x525F91). AUCUNE écriture non nulle, AUCUN `lea`
//   (donc aucun aliasing par pointeur) => `var_434 == 0` est INVARIANT => le `jnz`
//   (0x525FC0) n'est JAMAIS pris => 0x52601D — l'UNIQUE écriture de 1 dans +0xF57C de
//   toute l'image jeu (`search_text 0F57Ch` sur [0x401000,0x6D7234) = 7 hits, 4 écritures,
//   les 3 autres à 0) — est INATTEIGNABLE. Le panneau ne s'ouvre donc JAMAIS et
//   l'opcode 24 n'est JAMAIS émis par ce client.
//   => NE CÂBLER AUCUN CLIC vers OpenDeleteByNamePanel/ConfirmDeleteCharByName. Le code
//      ci-dessous est conservé (port fidèle, comportementalement correct car sans
//      appelant) — règle « une fonction morte dans le binaire reste morte en C++ ».
//
// [A12] L'assistant `dword_16692A4` n'est PAS un « indicateur compte de test/GM ».
//   PREUVE : `data_refs(0x16692A4)` = 6 refs. L'unique producteur est Net_LoginRequest
//   0x51B8E0, EA 0x51BBE7 : `Crt_Memcpy(&dword_16692A4, &g_NetClient.recvBuf + 0x95, 4)`.
//   Le payload commence à recvBuf+1, donc c'est le champ à l'OFFSET 148 DE LA RÉPONSE DE
//   LOGIN. Les 4 autres refs sont des `mov ds:dword_16692A4, 0` dans Net_AccountReq_op13/
//   14/15/16 (les opcodes de MOT DE PASSE SECONDAIRE). C'est donc un écran de PIN/mot de
//   passe secondaire OBLIGATOIRE pour tout compte qui en possède un — pas un overlay GM.
//   L'état est modélisé a minima ci-dessous (CharSelectPinState) ; les opcodes 13/14/15
//   ne sont PAS câblés (TODO ancré).
//
// PÉRIMÈTRE : logique de flux/décision pure. Pas de rendu, pas de D3D9. Tout effet de
// bord (réseau, UI, focus clavier, données de personnages) passe par CharSelectHost —
// SAUF deux globals d'état client déjà réifiés ailleurs et utilisés directement par le
// .cpp, car ce sont les MÊMES globals que le binaire touche et la fidélité du FLUX PRNG
// et de l'anti-rejeu en dépend :
//   net::g_MorphInProgress (Net/ClientState.h) = g_MorphInProgress 0x1675A88
//   net::DefaultRng()      (Net/Rng.h)          = Rng_Next 0x7603FD (_holdrand partagé)
// Précédent établi : Game/ComboPickupTick.h et UI/LoginScene.cpp font déjà exactement ça.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ts2::game {

// Nombre d'emplacements de personnage du flux standard. CONFIRMÉ par TROIS boucles
// `i<3` indépendantes : occupation initiale (EA 0x51bec4), sélection par défaut
// (EA 0x51c2ca), et la boucle de rendu des slots (`cmp var_20, 3` EA 0x51D526).
inline constexpr int32_t kMaxCharSlots = 3;

// Taille de la fiche personnage brute (0x2768). EA 0x51c707 (`Crt_Memcpy(..., 0x2768u)`).
inline constexpr int32_t kCharRecordSize = 10088;

// Sous-états internes de Scene_CharSelectUpdate (= this[1]). Cf. l'asymétrie en tête.
enum class CharSelectSubState : int32_t {
    Init   = 0, // Update compte 30 frames puis initialise ; RENDU = ÉCRAN VIDE ; souris inerte
    Active = 1, // seul état interactif
    Locked = 2, // Update inerte + souris inerte, mais RENDU COMPLET (image figée) = modal gelé.
                // Sortie UNIQUEMENT par le clic OK d'un NoticeDlg de mode 2 (hors scène).
};

// Type de NoticeDlg — 2e argument de UI_NoticeDlg_Open 0x5C0280. DÉTERMINE LE FLUX :
// c'est UI_NoticeDlg_OnLButtonUp 0x5C03F0 qui, sur OK, applique l'effet.
enum class NoticeType : int32_t {
    Close      = 1, // simple fermeture — la scène RESTE dans son état courant
    Disconnect = 2, // Net_CloseSocket + scène 2 (ServerSelect) + sous-état 0 (EA 0x5C04DF-0x5C04F8)
    Quit       = 3, // g_QuitFlag=1 — jamais utilisé par CharSelect
};

// Écran actif dans le sous-état Actif (this[15714], +0xF588, valeurs EXACTES 1/2).
enum class CharSelectScreen : int32_t {
    List       = 1, // liste des 3 emplacements + colonne de 10 boutons
    CreateForm = 2, // assistant de création
};

// État d'animation de l'aperçu 3D = this[15718] (+0xF598). ATTENTION AU NOM : ce champ
// est l'`animState` passé à PcModel_ResolveSlotAndApply/Char_RenderModel, PAS l'index de
// motion (celui-ci est this[15717], cf. CharSelectState::previewMotionIndex).
// CharSelect n'utilise QUE les valeurs 1 et 3 (this[15718]=1 à l'Init EA 0x51c363 ;
// =3 à l'armement d'ENTRER EA 0x52516F). La valeur 0 n'est JAMAIS employée ici.
enum class PreviewMotion : int32_t {
    Idle     = 1, // boucle cosmétique (le minuteur BOUCLE, EA 0x51c4e7-0x51c5bb)
    Entering = 3, // entrée armée ; à complétion du minuteur -> séquence réseau
};

// VERROU UNIVERSEL (this[15718] == 3) : une fois l'entrée armée, TOUS les boutons de la
// liste sortent avant toute action — ENTRER 0x5250A2, CRÉER 0x52523E, SUPPRIMER
// 0x525484, RENOMMER 0x525544, RETOUR 0x525A33, QUITTER 0x525ABE, RESTAURER 0x525B5A,
// bouton8 0x525CA4. L'écran entier est gelé. Garde d'atomicité de la transition.

// Transition de scène prête à être appliquée par l'appelant (consommée une seule fois).
enum class CharSelectTransition : int32_t {
    None,
    EnterWorld,   // this[0]=5 (EA 0x51c888) ; consulter enterWorld*
    ServerSelect, // this[0]=2 (EA 0x525A51) — bouton RETOUR ; cf. RequestBackToServerSelect
};

// Un emplacement de personnage. Les offsets sont ceux de la fiche brute de 10088 o
// (base unk_1669380 + 10088*i), NOMMÉS PAR LE MEMCPY 0x51c707 qui recopie la fiche du
// slot sélectionné dans g_SelfCharInvBlock 0x1673170 (donc record[+N] ≡ 0x1673170+N).
struct CharSlotInfo {
    // occupation : `Crt_Strcmp(&unk_1669394 + 10088*i, "") != 0` — RÈGLE UNIQUE ET
    // UNIVERSELLE (EA 0x51bec4/0x51c2f7 ; `data_refs(0x1669394)` = 10 refs, toutes dans
    // les 4 fonctions CharSelect ; aucun autre discriminant n'existe).
    bool    occupied = false;
    std::string name;         // +20  (unk_1669394) — 13 o max côté serveur

    // +56 (dword_16693B8) — critère de la sélection par défaut (comparaison STRICTE >,
    // EA 0x51c343). NOM TROMPEUR CONSERVÉ pour ne pas casser les appelants : la valeur
    // est en réalité le NIVEAU du personnage (elle alimente g_SelfLevel 0x16731A8).
    int32_t power = 0;

    // +36 — champ « job/classe ». C'est le champ ÉMIS par le formulaire de création
    // (dword_16709DC) et la SENTINELLE testée `== 3` par la branche LISTE de
    // Char_RenderModel (EA 0x52754A). Ce n'est PAS l'index de race du rendu de la liste.
    int32_t job = 0;

    // +40 (dword_16693A8) — RACE EFFECTIVE DU RENDU ET DU SON DE LA LISTE.
    // ⚠ DISTINCT de `job` (+36). PREUVE À TROIS TÉMOINS CONVERGENTS :
    //   1. Char_RenderModel 0x527020 a DEUX branches (`cmp [ebp+arg_4],0 ; jz` @0x52702F) :
    //      branche CRÉATION lit +36 (`mov edx,[ecx+24h]` @0x527051), branche LISTE lit
    //      +40 (`mov eax,[edx+28h]` @0x527536) — les deux passent +44 en genre.
    //   2. Scene_CharSelectUpdate appelle PcModel_ResolveSlotAndApply avec
    //      dword_16693A8[2522*slot] (=+40) et dword_16693AC[2522*slot] (=+44) — EA 0x51c555.
    //   3. Le son du bouton ENTRER passe les MÊMES 16693A8/16693AC (EA 0x5251E4).
    // La branche LISTE lit AUSSI +36, mais SEULEMENT comme sentinelle `==3` (0x52754A) :
    // les deux champs coexistent avec des rôles distincts — ce n'est pas un copier-coller.
    // +40 n'est JAMAIS écrit par le client (`data_refs(0x16709E0)` = 0 réf) : le serveur
    // le remplit (écho op17 EA 0x52A71E).
    // À PEUPLER par net::ParseCharRecord depuis l'offset 40 (cf. TODO K1 du front Net/).
    int32_t race = 0;

    int32_t faction = 0;      // +44 (dword_16693AC) — NOM TROMPEUR CONSERVÉ : c'est le
                              // GENRE (0..1). PcModel_ResolveEquipSlot 0x4E46A0 borne
                              // a3>1 (EA 0x4E46CC). L'offset d'émission est correct.
    int32_t face      = 0;    // +48 (0..6)
    int32_t hairColor = 0;    // +52 (0..2)

    // +104 (unk_16693E8) — bloc d'équipement de 208 o. Son champ +112 (= fiche +216) est
    // l'ID D'OBJET DE L'ARME DE DÉPART, résolu dans la DB items (`MobDb_GetEntry(mITEM)`
    // EA 0x5274A3). Consommé par Weapon_ClassFromField112 0x4CC870 (EA 0x525156) pour
    // dériver la classe d'arme -> l'index de motion de l'animation d'entrée.
    // Non réifié ici : exposé via CharSelectHost::GetEnterPreviewWeaponClass.

    int32_t localZoneId = 0;  // +5468 (dword_166A8DC) — pré-seed de g_TargetZoneId 0x1675A9C
                              // (EA 0x51c756). Sur op22 code 0 le SERVEUR l'écrase
                              // (Net_ReqEnterCharInfo écrit ses out-params UNIQUEMENT
                              // sous `if (!v19)`, EA 0x52b2b7-0x52b2ec).
    float   localPosX = 0.0f; // +5472 (dword_166A8E0) — int32 -> float par `fild` (EA 0x51c79e)
    float   localPosY = 0.0f; // +5476 (dword_166A8E4) — EA 0x51c7b9
    float   localPosZ = 0.0f; // +5480 (dword_166A8E8) — EA 0x51c7d4
};

// Formulaire de création = la fiche dword_16709B8 (fiche brute de 10088 o, elle aussi).
// INVENTAIRE EXHAUSTIF des champs que le binaire écrit (le reste part à ZÉRO) :
//   nom       byte_16709CC +20  <- GetWindowTextA(hwnd, buf, 13)  EA 0x526583/0x52658F
//   job/race  dword_16709DC +36 <- Rng_Next()%3 EA 0x52537C ; flèches 0x5260B2/0x526158
//   genre     dword_16709E4 +44 <- 0 EA 0x525382 ; flèches 0x5261F8/0x526280
//   visage    dword_16709E8 +48 <- 0 EA 0x52538C ; flèches 0x526305/0x52636B
//   cheveux   dword_16709EC +52 <- 0 EA 0x525396 ; flèches 0x5263D1/0x52643A
//   équipement unk_1670A20 +104 <- Crt_Memset(...,0,0xD0) EA 0x526634
//   arme      dword_1670A90 +216 <- 6*job + variant + 5   EA 0x52669A..0x52675B
// +56 n'est JAMAIS écrit par le formulaire. +40 non plus (le serveur le remplit).
struct CharCreateForm {
    int32_t job       = 0; // dword_16709DC (+36), 0..2
    int32_t faction   = 0; // dword_16709E4 (+44), 0..1 — c'est le GENRE (nom conservé)
    int32_t face      = 0; // dword_16709E8 (+48), 0..6
    int32_t hairColor = 0; // dword_16709EC (+52), 0..2

    // `variant` N'EST PAS DANS LA FICHE : c'est le champ de SCÈNE this[15716] (+0xF590),
    // bornes 0..2 (gardes `cmp ...,0` EA 0x526490 et `cmp ...,2` EA 0x52650B). Il est
    // conservé ici par commodité de modélisation, mais son cycle de vie est celui d'un
    // champ de scène : il n'atteint le réseau QUE via l'arme (+216 = 6*job+variant+5).
    // Il n'est PAS remis à zéro à l'Init de la scène (`search_text 0F590h` = 16 hits,
    // AUCUN dans Scene_CharSelectUpdate) mais l'EST par le bouton CRÉER (EA 0x52535E)
    // ET par les DEUX flèches de job (EA 0x5260DC et 0x526182).
    int32_t variant   = 0;
    std::string name;      // lu par host.GetEditedName()
};

// --- Helpers de LIBELLÉS du formulaire (Scene_CharSelectRender 0x51CED0) ---
// Les CINQ champs sont du TEXTE LOCALISÉ (aucun "%d" brut dans le binaire).
// job (0..2)     -> StrTable005_Get(g_LangId, 23/24/25).            EA 0x51e548-0x51e5c9
inline int32_t CreateJobLabelStrId(int32_t job) { return 23 + job; }
// faction (0..1) -> StrTable005_Get(g_LangId, 26/27).                EA 0x51e60b-0x51e64e
inline int32_t CreateFactionLabelStrId(int32_t faction) { return 26 + faction; }
// variant (0..2) -> grille 3x3 par (job,variant) : 29+3*job+variant. EA 0x51e76c-0x51e93e
// DISTINCT de l'id RÉSEAU du préréglage d'arme (5..19) : deux grilles indépendantes.
inline int32_t CreateVariantLabelStrId(int32_t job, int32_t variant) { return 29 + 3 * job + variant; }
// face ET hairColor partagent `sprintf("%c %s", 'A'+valeur, StrTable005_Get(g_LangId,28))`
// EA face 0x51e690-0x51e6f8, EA hairColor 0x51e6fd-0x51e767 (MÊME mot fixe id 28).
inline constexpr int32_t kCreateFaceHairLabelWordStrId = 28;
inline char CreateFaceHairLabelLetter(int32_t value) { return static_cast<char>('A' + value); }

// Résultat de Net_ReqEnterCharInfo (opcode 22, réponse 17 o = [1][code:4][domainId:4]
// [gamePort:4][zoneId:4]). ⚠ Les 3 out-params ne sont écrits QUE si code==0 (garde
// `if (!v19)` EA 0x52b2b7) — sur code non nul, domainId/gamePort/zoneId gardent la
// valeur pré-existante de l'appelant.
struct EnterCharInfoResult {
    int32_t resultCode = 101; // 0=ok ; 1=échec DOUX (retour Idle) ; 2/3/4/101/102=verrouillant
    int32_t domainId   = 0;   // -> Net_SelectServerDomain 0x53FE90
    int32_t gamePort   = 0;   // -> Net_ConnectGameServer 0x462A70
    int32_t zoneId     = 0;   // AUTHENTIQUE (écrase le pré-seed local, cf. CharSlotInfo)
};

// --- Points d'intégration (réseau/UI/données), nullptr = repli sûr documenté ---
struct CharSelectHost {
    // Peuple `slots` à l'entrée en scène (miroir des lectures directes de unk_1669394/
    // dword_16693A8/16693AC/16693B8/166A8DC.. du binaire). Câblé sur
    // net::LoadCharacterSlotsFromRecords. nullptr => slots inchangés.
    std::function<void(std::array<CharSlotInfo, kMaxCharSlots>&)> LoadCharacterSlots;

    // Net_AccountKeepAlive 0x5298F0 (op12, /30 frames). 101 = session expirée.
    // nullptr => traité comme vivant.
    std::function<int32_t()> AccountKeepAlive;

    // Ac_GameGuard_Heartbeat 0x6DE3F7, appelé /30 frames DANS LA MÊME FRAME que le
    // keepalive (EA 0x51c46e). Le binaire compare le retour à 1877 (`cmp eax, 755h`
    // EA 0x51c469) et pose g_QuitFlag=1 sinon (EA 0x51c470).
    // L'anticheat est HORS PÉRIMÈTRE (CLAUDE.md) : nullptr => kGameGuardHeartbeatOk,
    // c'est-à-dire « le battement réussit toujours ». La STRUCTURE du flux est fidèle,
    // seul le verdict est neutralisé.
    std::function<int32_t()> GameGuardHeartbeat;

    // g_QuitFlag = 1 (0x815590). Utilisé par l'échec GameGuard (EA 0x51c470) ET par le
    // bouton QUITTER. nullptr => no-op.
    std::function<void()> RequestQuit;

    // Str_ValidateNameChars 0x53FD70 sur le nom STOCKÉ du slot (EA 0x525109) —
    // précondition d'ENTRER. nullptr => true.
    std::function<bool(int32_t slotIndex)> IsCharacterNameValid;

    // g_GmAuthLevel >= 1 (0x1669294) — `cmp g_GmAuthLevel, 1 ; jge` EA 0x5250E2 : un GM
    // SAUTE entièrement la validation du nom. nullptr => false.
    std::function<bool()> HasGmAuthLevel;

    // UI_NoticeDlg_Open(byte_18225C8, type, StrTable005_Get(g_LangId,strId), "") 0x5C0280.
    // PRÉFÉRÉ à ShowNotice : le TYPE est ce qui décide si Verrouillé est un cul-de-sac
    // (mode 1) ou une sortie vers ServerSelect (mode 2), cf. NoticeType.
    // Si nullptr, on retombe sur ShowNotice (le type est alors PERDU -> l'état Verrouillé
    // devient un cul-de-sac : défaut de fidélité, cf. wiring TODO NOTICEDLG_MODE2).
    std::function<void(int32_t strId, NoticeType type)> ShowNoticeTyped;

    // Variante historique SANS type. Conservée pour les hôtes déjà câblés
    // (UI/LoginScene.cpp:1732). Utilisée uniquement si ShowNoticeTyped est absent.
    std::function<void(int32_t strId)> ShowNotice;

    // UI_MsgBox_Open(2, StrTable005_Get(g_LangId,49), "") : confirmation Oui/Non de
    // suppression. L'hôte DOIT rappeler ConfirmDeleteCharacter() sur « Oui » (le binaire
    // route cela via UI_MsgBox_OnLButtonUp 0x5C0A90 -> CharSelect_ReqDeleteChar 0x528FD0).
    std::function<void()> ShowDeleteConfirm;

    // Str_ValidateNameChars 0x53FD70 sur le nom SAISI en création. Implémentation fidèle
    // fournie : ValidateNameCharset(). nullptr => true (non fidèle, hôtes de test).
    std::function<bool(const std::string&)> ValidateNameChars;

    // maybe_Dict001_MatchWord(g_BannedWordList, nomSaisi) 0x4C1410 (EA 0x5265FC) —
    // true = banni. nullptr => false (non fidèle, hôtes de test).
    std::function<bool(const std::string&)> IsNameBanned;

    // GetWindowTextA(dword_1668FCC, buf, 13) — nom saisi dans le formulaire de création.
    std::function<std::string()> GetEditedName;

    // Rng_Next()%3 (EA 0x52537C) — job initial aléatoire à l'ouverture du formulaire.
    // ⚠ CONSOMME LE FLUX PRNG PARTAGÉ : doit taper sur net::DefaultRng(). nullptr => 0
    // (et le flux PRNG DÉCALE d'un tirage par rapport au binaire).
    std::function<int32_t()> RandomInitialJob;

    // Efface les 150 latches de boutons this[3..152] (+12..+608) à chaque Init —
    // boucle `for (i=0;i<150;++i) this[i+3]=0` EA 0x51be83-0x51bea4 (`cmp var,96h`).
    // ⚠ CE N'EST PAS 10 : OnMouseDown arme des latches jusqu'à this[92]. Les latches sont
    // les Widget de UI/LoginScene, hors périmètre de ce module => hook.
    // nullptr => no-op (les latches restent armés d'une visite à l'autre : défaut).
    std::function<void()> ClearAllButtonLatches;

    // UI_FocusEditBox(&g_UIEditBoxMgr, index) 0x50F4A0. Index PROUVÉS : 0 à l'Init
    // (EA 0x51be7e), 19 à l'ouverture du panneau mort (EA 0x525fcc), 0 au succès op24
    // (EA 0x529365). `UI_FocusEditBox` ne fait QUE SetFocus.
    std::function<void(int32_t editBoxIndex)> FocusEditBox;

    // PcModel_ResolveSlotAndApply 0x4E5A00 — renvoie le NOMBRE DE FRAMES de l'animation.
    // Le binaire l'appelle avec des arguments DIFFÉRENTS selon l'écran (EA 0x51c555 pour
    // la liste, EA 0x51cd7a pour la création) :
    //   LISTE    : (g_ModelMotionArray, rec+40 /*race*/, rec+44 /*genre*/, motion, animState, 1,0,0,1)
    //   CRÉATION : (g_ModelMotionArray, dword_16709DC /*+36*/, dword_16709E4 /*+44*/, motion, animState, 1,0,0,1)
    // D'où la signature générique ci-dessous. Les 4 derniers arguments sont CONSTANTS.
    // nullptr => repli sur GetEnterPreviewDurationFrames puis kDefaultEnterPreviewFrames.
    std::function<int32_t(int32_t modelRace, int32_t modelGender, int32_t motion, int32_t animState)>
        GetMotionFrameCount;

    // OBSOLÈTE — conservé parce que UI/LoginScene.cpp:1807 l'assigne encore (à nullptr).
    // Repli de GetMotionFrameCount, utilisé UNIQUEMENT pour l'animation d'ENTRÉE.
    std::function<float(int32_t slotIndex)> GetEnterPreviewDurationFrames;

    // Weapon_ClassFromField112(g_EquipSnapshotScratch, &unk_16693E8 + 10088*slot) 0x4CC870
    // (EA 0x525156) -> classe d'arme 1..3, lue depuis l'ID d'objet à rec+104+112 (=rec+216).
    // L'index de motion de l'animation d'entrée en est dérivé : `shl eax,1` (EA 0x52515B)
    // puis `this[15717] = 2*classe` (EA 0x525163).
    // nullptr => TODO [ancre 0x4CC870] : previewMotionIndex reste à 0 (l'animation d'entrée
    // joue alors le motion 0 au lieu de 2/4/6). Nécessite la DB items (Game/GameDatabase,
    // hors périmètre de ce front).
    std::function<int32_t(int32_t slotIndex)> GetEnterPreviewWeaponClass;

    // Latches des DEUX bascules de rotation de l'aperçu de création : this[15] (EA
    // 0x51cdd0) et this[16] (EA 0x51cdf1). Widgets de LoginScene => hooks.
    // nullptr => false (pas de rotation).
    std::function<bool()> IsRotateLeftLatched;
    std::function<bool()> IsRotateRightLatched;

    // `dword_16692A4 != 0` (EA 0x51beae) — l'assistant de MOT DE PASSE SECONDAIRE est-il
    // requis ? Cf. [A12] en tête : ce global vient de la RÉPONSE DE LOGIN (offset 148,
    // `Crt_Memcpy(&dword_16692A4, &recvBuf + 0x95, 4)` EA 0x51BBE7) et est remis à 0 par
    // les opcodes 13/14/15/16. Ce n'est PAS un drapeau GM/compte de test.
    // TODO CÂBLAGE [ancre 0x51BBE7] : dword_16692A4 n'est pas réifié côté Net/ (Login.cpp
    // ne conserve pas ce champ de la réponse). nullptr => false = flux standard, ce qui est
    // correct pour un compte SANS mot de passe secondaire mais FAUX pour un compte qui en a.
    std::function<bool()> IsSecondaryPasswordRequired;

    // `Crt_Strcmp(&unk_16692A8, "") != 0` (EA 0x51bf3d) — un PIN est-il DÉJÀ défini sur le
    // compte ? unk_16692A8 = 5 o recopiés de la réponse de login (recvBuf+0x99 => payload
    // +152, EA 0x51bbfb). Décide du mode de l'assistant : true -> VÉRIFIER (2), false ->
    // DÉFINIR (1). nullptr => false.
    std::function<bool()> HasStoredSecondaryPassword;

    // g_ServerModeFlag (dword_166918C, = GameConfig::buildVariant) — EA 0x51c08d.
    // nullptr => false.
    std::function<bool()> IsServerModeFlag;

    // this[15374] (+0xF038) — index serveur à plat (valeurs observées 40/50/60).
    // EA 0x51c09d/0x51c13f. nullptr => 0.
    std::function<int32_t()> GetServerIndex;

    // Net_CreateCharacter(slot, fiche, &code) 0x52A4A0 (op17).
    std::function<int32_t(int32_t slotIndex, const CharCreateForm& form, int32_t lookPresetId)> CreateCharacter;

    // CharSelect_ReqDeleteChar 0x528FD0 -> Net_CharSlotAction(slot,1,0,&code) (op18).
    std::function<int32_t(int32_t slotIndex)> DeleteCharacter;

    // CharSelect_ReqRestoreChar 0x5295D0 -> Net_CharSlotAction(slot,2,listIndex,&code)
    // (op18, EA 0x5295f6). `listIndex` = this[15704] (+0xF560).
    // TODO CÂBLAGE [ancre 0x5295f6] : hook non branché ; la LISTE de restauration qui
    // alimente restoreListIndex n'est pas modélisée (cf. CharSelectState).
    std::function<int32_t(int32_t slotIndex, int32_t listIndex)> RestoreCharacter;

    // Net_ReqVerifyCharName(slotEnc, name, &code) 0x52B4C0 (op24).
    // 🔴 CHAÎNE MORTE — NE JAMAIS CÂBLER (cf. [H1] en tête de fichier).
    std::function<int32_t(int32_t slotEnc, const std::string& name)> VerifyCharName;

    // GetWindowTextA(dword_166900C, String, 49) 0x529273 — zone EDIT du panneau MORT
    // (≠ GetEditedName qui lit dword_1668FCC sur 13 o). 🔴 CHAÎNE MORTE.
    std::function<std::string()> GetDeleteByNameInput;

    // SetWindowTextA(dword_166900C, "") EA 0x525fe3. 🔴 CHAÎNE MORTE.
    std::function<void()> ClearDeleteByNameInput;

    // Publie l'identité du perso choisi dans l'état monde AVANT le handshake du serveur
    // de jeu. Miroir du memcpy UNIQUE EA 0x51c707 :
    //   Crt_Memcpy(g_SelfCharInvBlock /*0x1673170*/, &unk_1669380 + 10088*slot, 0x2768u)
    // Ce memcpy pose À LA FOIS le bloc d'inventaire ET g_LocalElement (= bloc+0x24 =
    // fiche[+36] = le champ `job`), qui part ensuite sur le fil aux octets [137..140] du
    // paquet d'auth op11 de Net_ConnectGameServer 0x462A70 (EA 0x462d5d).
    // ORDRE PROUVÉ : 0x51c707 (memcpy) ≺ 0x51c81d (op22) ≺ 0x51c850 ≺ op11.
    // 🔴 TODO CÂBLAGE [ancre 0x51c707] — NON BRANCHÉ à ce jour (défaut GAMEAUTH_Element_Zero) :
    // UI/LoginScene.cpp::BuildCharSelectHost doit poser
    //   charHost_.PublishSelfFromSlot = [](int32_t slot) {
    //       game::g_World.self.element = charState_.slots[slot].job;  // fiche +36
    //       /* + recopie de net::g_CharRecords[slot] dans g_World.self.charInvBlock */ };
    // Tant que ce hook est nullptr, les octets [137..140] du handshake restent à 0.
    std::function<void(int32_t slotIndex)> PublishSelfFromSlot;

    // Net_ReqEnterCharInfo(slot,&domainId,&port,&zoneId,&code) 0x52B070 (op22).
    std::function<EnterCharInfoResult(int32_t slotIndex)> RequestEnterCharInfo;

    // Net_SelectServerDomain(domainId,&host) 0x53FE90 (EA 0x51c850) +
    // Net_ConnectGameServer(&g_NetClient,host,port,&code) 0x462A70 (EA 0x51c866), repliés.
    std::function<int32_t(int32_t domainId, int32_t gamePort)> ConnectToGameServer;

    // Net_ReqCancelEnter(&code) 0x52B310 (op23) — EA 0x51c93c/0x51c9e9/0x51ca96.
    std::function<int32_t()> CancelEnter;

    // Net_CloseSocket(&g_NetClient) 0x463000 + g_QuitFlag=1 — bouton QUITTER.
    std::function<void()> CloseConnectionAndQuit;

    // Net_CloseSocket(&g_NetClient) 0x463000 SEUL — bouton RETOUR (EA 0x525A46), avant
    // la transition vers ServerSelect. nullptr => no-op.
    std::function<void()> CloseConnection;
};

// Valeur de succès du battement GameGuard (`cmp eax, 755h` EA 0x51c469).
inline constexpr int32_t kGameGuardHeartbeatOk = 1877;

// Slots d'atlas du fond plein écran (EA 0x51c261/0x51c270/0x51c27f). Déclarés AVANT
// CharSelectState : kCharBgSlotFirst sert d'initialiseur de membre par défaut.
inline constexpr int32_t kCharBgSlotFirst = 2383;
inline constexpr int32_t kCharBgSlotCount = 3;

// --- État de l'assistant PIN / mot de passe secondaire (branche dword_16692A4 != 0) ---
// Cf. [A12] en tête : écran OBLIGATOIRE pour tout compte doté d'un mot de passe
// secondaire, PAS un overlay GM. Modélisé A MINIMA (l'état, pas les interactions).
struct CharSelectPinState {
    bool    panelOpen = false; // this[15375] (+0xF03C) — 1 = assistant PIN actif (EA 0x51bf1c)
    // this[15376] (+0xF040) — 1 = DÉFINIR un PIN (aucun PIN stocké), 2 = VÉRIFIER.
    // Discriminant EXACT : `Crt_Strcmp(&unk_16692A8, "")` (EA 0x51bf3d) où unk_16692A8 est
    // le PIN de 5 o recopié depuis la réponse de login (recvBuf+0x99 => payload +152,
    // EA 0x51bbfb). Non vide -> 2 (EA 0x51bf68) ; vide -> 1 (EA 0x51bf4c).
    int32_t mode      = 0;
    int32_t step      = 0; // this[15377] (+0xF044) = 0 (EA 0x51bf59)
    // Permutation ALÉATOIRE du pavé numérique this[15385..15394] (+0xF064..+0xF088) :
    // init à -1 (EA 0x51c008) puis remplissage par rejet `do v=Rng_Next()%10 while(occupé)`
    // (EA 0x51c015-0x51c05f). ⚠ CONSOMME LE FLUX PRNG PARTAGÉ — reproduit tel quel.
    std::array<int32_t, 10> keypad{};
    // TODO [ancres 0x529AA0 / 0x529D20 / 0x529FB0] : les opcodes 13 (vérifier), 14
    // (changer), 15 (définir) ne sont PAS câblés — l'assistant est un état inerte.
    // TODO [ancre 0x51bf82] : this[15378..15380] (3 × char[5] à this+61524/61529/61534,
    // les 3 saisies) et this[15395] (EA 0x51c06f) non modélisés.
};

// --- État complet de l'écran CharSelect ---
struct CharSelectState {
    CharSelectSubState subState = CharSelectSubState::Init;
    int32_t frameCounter = 0; // this[2] — ⚠ N'EST PAS remis à 0 par le keepalive réussi

    CharSelectScreen screen = CharSelectScreen::List; // this[15714] (+0xF588)

    std::array<CharSlotInfo, kMaxCharSlots> slots{};
    int32_t selectedSlot = -1; // this[15715] (+0xF58C), -1 = aucun

    // this[15705] (+0xF564) — 🔴 CHAMP MORT : `search_text 0F564h` sur [0x401000,0x6D7234)
    // = 5 hits, 5 ÉCRITURES (0x51bf0a=1, 0x51bf29=0, 0x5230be=1, 0x52335e=1, 0x5237e9=1),
    // ZÉRO LECTURE. Conservé comme miroir fidèle en ÉCRITURE SEULE.
    //
    // ⚠ NOM CORRIGÉ — il s'appelait `allSlotsFull` : c'était EXACTEMENT L'INVERSE.
    //   La boucle EA 0x51bec4-0x51bf0a avance sur les slots VIDES et SORT au premier
    //   OCCUPÉ : `Crt_Strcmp(&unk_1669394 + 0x2768*i, "")` EA 0x51bef1 ; `test eax,eax`
    //   EA 0x51bef9 ; `jz short loc_51BEFF` EA 0x51befb -> `jmp short loc_51BECD` (i++,
    //   CONTINUE sur nom VIDE) ; sinon `jmp short loc_51BF01` EA 0x51befd (nom NON vide
    //   = slot OCCUPÉ -> SORT). Donc `cmp [ebp+var_10], 3` EA 0x51bf01 + `jl` EA 0x51bf05
    //   ne laisse tomber sur `mov [eax+0F564h], 1` EA 0x51bf0a QUE si LES 3 SLOTS SONT
    //   VIDES. Le drapeau est un « aucun personnage sur le compte », pas un « liste pleine ».
    //   CONTRE-ÉPREUVE dans la MÊME image : la boucle « reste-t-il un slot libre ? » du
    //   bouton CRÉER (EA 0x52524c-0x525289) est le MIROIR EXACT de polarité — son
    //   `jnz short loc_525287` EA 0x525283 fait CONTINUER sur les slots OCCUPÉS et sortir
    //   au premier VIDE. Les deux boucles sont donc délibérément opposées : ce n'est pas
    //   un copier-coller, et l'une ne peut pas servir de modèle à l'autre.
    // ⚠ NE JAMAIS LE LIRE ni le faire gater quoi que ce soit (règle « mort = mort ») :
    //   le bouton CRÉER teste la présence d'un slot libre (EA 0x52524c), pas ce drapeau.
    bool allSlotsEmpty = false;

    // this[15713] (+0xF584) — slot d'atlas du FOND plein écran, TIRÉ À CHAQUE INIT :
    // `v20 = Rng_Next()%3` (EA 0x51c247) -> 2383 (0x51c261) / 2384 (0x51c270) / 2385
    // (0x51c27f). Le bloc est À L'INTÉRIEUR de l'Init (immédiatement suivi de
    // this[15714]=1 EA 0x51c28c et this[15715]=-1 EA 0x51c299).
    // ⚠ Le `default` (EA 0x51c289) N'ÉCRIT RIEN — sans effet car Rng_Next() >= 0, mais
    // NE PAS « corriger » en ajoutant un cas par défaut.
    // Le tirage est fait ICI (et non dans LoginScene::Init) pour deux raisons : fidélité
    // temporelle ET synchronisation du flux PRNG partagé.
    // -> UI/LoginScene doit LIRE ce champ, pas re-tirer (wiring TODO CHARBG_SLOT).
    int32_t backgroundSlot = kCharBgSlotFirst;

    // --- Aperçu 3D ---
    // this[15717] (+0xF594) — INDEX DE MOTION. 0 à l'Init (EA 0x51c356) ; 2*classe d'arme
    // à l'armement d'ENTRER (EA 0x525163). ⚠ DISTINCT de previewMotion (= this[15718]).
    int32_t previewMotionIndex = 0;
    PreviewMotion previewMotion  = PreviewMotion::Idle; // this[15718] (+0xF598) = animState
    float         previewElapsed = 0.0f;                // this[15719] (+0xF59C), en frames
    // this[15720..15722] (+0xF5A0..A8) — position vec3 de l'aperçu, remise à 0 à l'Init.
    float previewPos[3] = {0.0f, 0.0f, 0.0f};
    // this[15723..15725] (+0xF5AC..B4) — rotation vec3. Le YAW est this[15724] (+0xF5B0)
    // (`fld dword ptr [ecx+4]` EA 0x527076 sur le bloc de rotation).
    float previewRot[3] = {0.0f, 0.0f, 0.0f};

    // Angle de spawn (flt_1675AC4 ET flt_1675AC8 — MÊME valeur dupliquée, PAS deux
    // angles). `Rng_Next()%360` TIRÉ DANS LE BLOC D'ENTRÉE (EA 0x51c7ed), AVANT
    // Net_ReqEnterCharInfo (EA 0x51c81d) — donc AVANT les 4 tirages de nonce de l'op22
    // et les 4 de l'op11. L'ordre du flux PRNG en dépend.
    // -> UI/LoginScene doit LIRE ce champ, pas re-tirer (wiring TODO SPAWN_ROT).
    float enterWorldSpawnRotationDeg = 0.0f;

    // --- Assistant PIN (cf. [A12]) ---
    CharSelectPinState pin{};

    // --- Panneau « suppression par saisie du nom » (opcode 24) ---
    // 🔴 INATTEIGNABLE (cf. [H1] en tête) : ces deux champs restent à leur valeur d'init.
    // Conservés comme port fidèle des champs +0xF57C / +0xF580 (EA 0x51c223 / 0x51c230).
    bool    deleteByNamePanelOpen = false; // this[15711] (+0xF57C)
    int32_t deleteByNameListFlag  = 0;     // this[15712] (+0xF580) — multiplicateur ×100

    // this[15704] (+0xF560) — index de sélection dans la liste de restauration, envoyé
    // comme `arg` de l'op18 action=2 (EA 0x5295f6). Init à -1 (EA 0x51c1e2).
    // TODO [ancres 0x524232-0x524250 / 0x5242ac-0x5242d8] : la LISTE elle-même n'est pas
    // modélisée (flèches précédent/suivant qui clampent dans [0, count-1]).
    int32_t restoreListIndex = -1;

    // this[15602] (+0xF3C8) — NOMBRE d'entrées de la liste de restauration. C'est le SEUL
    // champ LU de ce bloc (EA 0x5242ac). La table this[15603..15611] qui le suit est
    // écrite (EA 0x51c160-0x51c1c8 / 0x51c0be-0x51c126) mais JAMAIS lue : NON modélisée
    // (règle « mort = mort »). Formule EXACTE, cf. RecomputeRestoreListCount().
    // ⚠ MIROIR EN ÉCRITURE SEULE CÔTÉ C++ : le binaire le lit @0x5242ac (écran de
    //   RESTAURATION), qui n'est PAS porté — la chaîne entière est morte ici
    //   (host.RestoreCharacter jamais assigné, bouton 3086 non câblé). Ne pas lire ce champ
    //   tant que cette chaîne n'est pas portée ; sa valeur dépend en outre de deux hooks
    //   aujourd'hui nullptr (IsServerModeFlag/GetServerIndex), donc elle n'est pas fiable.
    int32_t restoreListCount = 5;

    CharCreateForm createForm{};

    // Transition consommable une seule fois par UpdateCharSelect().
    CharSelectTransition pendingTransition = CharSelectTransition::None;
    int32_t enterWorldZoneId = 0;
    int32_t enterWorldSlot   = -1;
};

// Str_ValidateNameChars 0x53FD70 — reproduction FIDÈLE. L'original :
//   1. MultiByteToWideChar(CP_ACP, 0, name, -1, buf, 13) — buffer FIXE de 13 WCHAR
//      (12 caractères utiles + NUL) : échoue si la conversion rate, ce qui INCLUT « nom
//      trop long ». Il n'y a AUCUNE autre garde de longueur dans le binaire — c'est la
//      capacité du buffer qui fait office d'encodage ET de borne dure à 12 caractères.
//   2. sub_760F03 = wcslen(buf).
//   3. CHAQUE caractère large doit être dans l'un des 4 intervalles EXACTS : '0'-'9'
//      (0x30-0x39), 'A'-'Z' (0x41-0x5A), 'a'-'z' (0x61-0x7A), ou le bloc thaï
//      U+0E00-U+0E7F (présent tel quel dans le binaire EU — reproduit sans interprétation).
// ÉCART DE COMPORTEMENT ASSUMÉ (fidèle, pas un bug de portage) : un nom VIDE traverse la
// boucle sans itération et renvoie TRUE. Ce n'est PAS cette fonction qui rejette le vide :
// c'est l'appelant (EA 0x52658f-0x5265b7, `GetWindowTextA(...) == 0` -> notice 38).
bool ValidateNameCharset(const std::string& name);

// Scene_CharSelectUpdate 0x51BD90. Appeler 1x/frame (30 FPS) tant que la scène est
// CharSelect. Retourne la transition prête (None la plupart du temps).
CharSelectTransition UpdateCharSelect(CharSelectState& state, const CharSelectHost& host, float dt);

// Clic sur un emplacement occupé de la liste. No-op si vide/hors bornes/déjà sélectionné.
void SelectCharacterSlot(CharSelectState& state, int32_t slotIndex);

// Bouton CRÉER (écran Liste). Ouvre le formulaire si un emplacement est libre
// (notice[22] sinon) ; job initial tiré via host.RandomInitialJob.
void OnCreateButtonClicked(CharSelectState& state, const CharSelectHost& host);

// Paires de boutons −/+ du formulaire. `delta` = -1 ou +1. Bornes EXACTES [0,2]/[0,1]/
// [0,6]/[0,2]/[0,2] — le binaire SORT SANS RIEN FAIRE aux bornes (`if (job==0) return`
// EA 0x52609b pour −, `if (job==2) return` EA 0x526141 pour +), il ne « clampe » pas.
// ⚠ SetCreateJob réinitialise genre/visage/cheveux **ET `variant`** ET le minuteur.
void SetCreateJob(CharSelectState& state, int32_t delta);
void SetCreateFaction(CharSelectState& state, int32_t delta);
void SetCreateFace(CharSelectState& state, int32_t delta);
void SetCreateHairColor(CharSelectState& state, int32_t delta);
void SetCreateVariant(CharSelectState& state, int32_t delta);

// Bouton CONFIRMER du formulaire. Ordre EXACT des 3 notices (EA 0x52658f-0x526623) :
// vide -> 38 ; caractères refusés -> 39 ; mot banni -> 40. Puis id d'arme (job,variant)
// et Net_CreateCharacter au premier emplacement libre.
bool ConfirmCreateCharacter(CharSelectState& state, const CharSelectHost& host);

// Bouton ANNULER du formulaire. Retour Liste, sélection INCHANGÉE (fidèle).
void CancelCreateForm(CharSelectState& state);

// Bouton SUPPRIMER (écran Liste). Ouvre la confirmation via host.ShowDeleteConfirm().
bool OnDeleteButtonClicked(CharSelectState& state, const CharSelectHost& host);

// Clic « Oui » de la confirmation ouverte par OnDeleteButtonClicked().
void ConfirmDeleteCharacter(CharSelectState& state, const CharSelectHost& host);

// --- Panneau « suppression par saisie du nom » (opcode 24) ---
// 🔴 CHAÎNE MORTE, INATTEIGNABLE DANS LE BINAIRE (preuve complète en tête, [H1]).
// Port fidèle CONSERVÉ mais qui doit RESTER SANS APPELANT. Ne câbler aucun clic.
void OpenDeleteByNamePanel(CharSelectState& state, const CharSelectHost& host);
void CancelDeleteByNamePanel(CharSelectState& state);
void ConfirmDeleteCharByName(CharSelectState& state, const CharSelectHost& host);

// Clic « Oui » de la confirmation de RESTAURATION (CharSelect_ReqRestoreChar 0x5295D0).
// Sans appelant tant que la liste de restauration n'est pas portée.
void ConfirmRestoreCharacter(CharSelectState& state, const CharSelectHost& host);

// Bouton ENTRER (écran Liste). Précondition : un slot sélectionné, pas déjà en entrée,
// et (nom stocké valide OU GM). Sur succès arme previewMotion=Entering : la séquence
// réseau part au MINUTEUR (dans UpdateCharSelect), JAMAIS ici — fidèle (EA 0x52516F).
bool OnEnterButtonClicked(CharSelectState& state, const CharSelectHost& host);

// Bouton RETOUR (écran Liste) -> scène 2 (ServerSelect), PAS Login(3), et SANS notice.
// Garde de sortie `this[15718]==3` (EA 0x525A33) puis Net_CloseSocket (EA 0x525A46),
// this[0]=2 (EA 0x525A51), this[1]=0 (EA 0x525A5D), this[2]=0 (EA 0x525A6A).
// Retourne true si la transition a été armée (à consommer via UpdateCharSelect).
bool RequestBackToServerSelect(CharSelectState& state, const CharSelectHost& host);

// Bouton QUITTER (écran Liste). No-op si en cours d'entrée (garde EA 0x525ABE).
void OnQuitButtonClicked(CharSelectState& state, const CharSelectHost& host);

// Applique l'effet du clic OK d'un NoticeDlg de MODE 2 (UI_NoticeDlg_OnLButtonUp
// 0x5C03F0 case 2). C'est la SEULE sortie de l'état Verrouillé, et elle arrive par une
// route HORS SCÈNE (UI_RouteLButtonUp 0x5AD0F0 -> 0x5C03F0, xref unique EA 0x5AD164) :
// les handlers souris de la scène sont gatés `== 1` et ne la verraient jamais.
// Effets EXACTS : Net_CloseSocket (0x5C04DF), scène 2 (0x5C04E4), sous-état 0 (0x5C04EE),
// compteur 0 (0x5C04F8).
// -> UI/LoginScene doit appeler ceci depuis son routage NoticeDlg (wiring TODO NOTICEDLG_MODE2).
void OnNoticeDlgMode2Ok(CharSelectState& state, const CharSelectHost& host);

} // namespace ts2::game

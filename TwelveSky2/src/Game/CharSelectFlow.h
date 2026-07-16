// Game/CharSelectFlow.h — flux d'état de l'écran de SÉLECTION DE PERSONNAGE (scène 4).
//
// Réécriture C++ fidèle du FLUX/DÉCISION (liste, sélection, création, suppression,
// entrée en jeu) de :
//   Scene_CharSelectUpdate      0x51BD90  (~4,2 Ko — machine d'état, PRIORITAIRE)
//   Scene_CharSelectOnMouseDown 0x520F40  (~8 Ko  — latches de boutons)
//   Scene_CharSelectOnMouseUp   0x522E50  (~15 Ko — validation clic + requêtes réseau)
//   Scene_CharSelectRender      0x51CED0  (~16 Ko — PURE D3D9 : Gfx_BeginFrame, matrices,
//                                          sprites/texte ; AUCUNE logique de flux trouvée
//                                          au sondage des refs — non porté, TODO rendu)
// Décompilées via idaTs2 (serveur MCP HTTP JSON-RPC brut, http://127.0.0.1:13337/mcp,
// méthode "decompile" — le deferred tool mcp__idaTs2__* n'était pas enregistré côté
// client dans cette session ; MÊME IDB `RE/TwelveSky2.exe.i64`, mêmes données).
// Fonctions réseau/action associées, également décompilées pour lever toute ambiguïté :
//   Net_CreateCharacter      0x52A4A0 (opcode 17, C->S slot+10088o, S->C code)
//   Net_CharSlotAction       0x52A740 (opcode 18, C->S slot+action+arg, S->C code)
//   CharSelect_ReqDeleteChar 0x528FD0 (appelle Net_CharSlotAction(slot,1,0,&code))
//   Net_ReqEnterCharInfo     0x52B070 (opcode 22, C->S slot, S->C domainId+port+zoneId)
//   Net_SelectServerDomain   0x53FE90 (résout domainId -> hôte, détail réseau bas niveau)
//   Net_ConnectGameServer    0x462A70 (connecte au serveur de jeu résolu)
//   Net_ReqCancelEnter       0x52B310 (annule une entrée en cours après erreur de connexion)
//   Net_AccountKeepAlive     0x5298F0 (opcode 12, heartbeat de session, /30 frames)
//
// FLUX D'ORIGINE (g_SceneMgr @0x1676180 pendant que sceneId==CharSelect(4) : this[1]=
// sous-état, this[2]=compteur de frames, this[15714]=écran actif, this[15715]=slot
// sélectionné (-1=aucun), this[15718]=sous-état d'aperçu du perso sélectionné) :
//
//   Sous-état 0 (Init) : attend 30 frames (0x1E) puis (re)calcule l'occupation des 3
//     emplacements (comparaison des noms this+15381.. à "" -> Crt_Strcmp), sélectionne
//     par défaut l'emplacement occupé de plus haute "puissance" (dword_16693B8, tri par
//     valeur décroissante, premier occupé si égalité), arme allSlotsFull si les 3 sont
//     pris, réinitialise l'aperçu 3D (this[15717..15725]=0) -> écran Liste(1),
//     sous-état d'aperçu Idle(1), passe en sous-état Actif(1).
//     ÉCART CONNU : le binaire a AUSSI une branche complète activée par le global
//     dword_16692A4 (vraisemblablement un indicateur "compte de test/GM" fourni par le
//     login) qui bascule this[15375]=1 et affiche un ASSISTANT ENTIÈREMENT DIFFÉRENT
//     (mot de passe secondaire à 3 essais via Net_AccountReq_op13/14/15, liste étendue
//     à 10 emplacements dword_16692B8 avec pagination 5 pages, suppression via
//     Net_AccountReq_op21(2,...)). DÉLIBÉRÉMENT NON REPRODUIT ICI : c'est un système
//     de compte annexe (mot de passe secondaire / overlay GM), pas le flux standard de
//     sélection de personnage visé par cette mission — documenté pour mémoire.
//   Sous-état 1 (Actif) : toutes les 30 frames, heartbeat de session
//     (Net_AccountKeepAlive, code 101 = session expirée -> notice[20] + sous-état
//     Verrouillé(2)) ; toutes les 30 frames également, heartbeat anti-triche
//     (Ac_GameGuard_Heartbeat) IGNORÉ ICI (hors périmètre CLAUDE.md — le binaire fait
//     g_QuitFlag=1 en cas d'échec, non modélisé). Puis, si écran==Liste ET aperçu==
//     Entrée(3) : fait avancer le minuteur d'aperçu ; à complétion, déclenche UNE FOIS
//     la séquence réseau d'entrée en jeu (Net_ReqEnterCharInfo -> ConnectToGameServer),
//     cf. AdvanceEnterSequenceIfDue(). Si écran==Formulaire de création : rotation libre
//     de la caméra d'aperçu par les boutons flèche (PUREMENT visuel, TODO rendu, non
//     modélisé) — aucun impact sur le flux.
//   Sous-état 2 (Verrouillé) : terminal, AUCUN traitement (le binaire ne matche aucun
//     `case` pour cette valeur dans Update ET les deux handlers souris commencent par
//     `if (this[1] != 1) return` — donc plus aucune interaction possible). Atteint après
//     la plupart des erreurs réseau irrécupérables (session expirée, échec de connexion
//     au monde, création/suppression en échec serveur). Comportement identique au motif
//     déjà documenté dans EnterWorldFlow (état terminal "Failed").
//
//   Écran Liste (this[15714]==1, 3 emplacements) :
//     - Clic sur un emplacement occupé -> SelectCharacterSlot() (sélection simple,
//       aucune requête réseau).
//     - Bouton "Créer" -> OnCreateButtonClicked() : ouvre le formulaire (écran
//       Formulaire) si un emplacement est libre, sinon notice[22] "liste pleine".
//       ÉCART DE GARDE NON REPRODUIT : le binaire ajoute aussi
//       `this[15374]!=40 || g_ServerModeFlag` (notice[2163] sinon) — variante
//       régionale/build non confirmée, TODO ci-dessous.
//     - Bouton "Supprimer" -> OnDeleteButtonClicked() : si un emplacement est
//       sélectionné (et pas déjà en cours d'entrée), ouvre une confirmation
//       (host.ShowDeleteConfirm) ; ConfirmDeleteCharacter() doit être appelée par
//       l'appelant sur clic "Oui" -> Net_CharSlotAction(slot,1,0,&code) (opcode 18).
//     - Bouton "Entrer en jeu" -> OnEnterButtonClicked() : si un emplacement est
//       sélectionné et (nom du perso valide OU autorisation GM), arme l'aperçu
//       "Entrée"(3) qui déclenchera la séquence réseau au minuteur suivant. Sinon
//       notice[1856] (nom invalide) ou notice[47] (aucune sélection).
//     - Bouton "Quitter" -> OnQuitButtonClicked() : ferme la socket + demande la
//       fermeture de l'application (g_QuitFlag=1), SEULEMENT si pas en cours d'entrée.
//     - Panneau "suppression par saisie du nom" (this[15711] = +0xF57C) : ÉLUCIDÉ et
//       PORTÉ (il était noté « MsgBox 41, contexte non élucidé »). C'est un SECOND
//       mécanisme de suppression, à double confirmation, distinct de l'opcode 18
//       action=1 : le joueur doit RETAPER le nom du personnage, puis valider une
//       MsgBox. Chaîne prouvée EA par EA :
//         ouverture du panneau (EA 0x525fc0-0x52602d) : Util_SetClampedU8Field(
//           dword_8E714C,0) · UI_FocusEditBox(&g_UIEditBoxMgr, 19) · SetWindowTextA(
//           dword_166900C,"") · 150 dwords à this+12 remis à 0 · this[15711]=1 ET
//           this[15712]=1 (+0xF57C/+0xF580, armés ENSEMBLE, bloc rectiligne).
//         -> saisie du nom, puis clic de validation : le bloc EA 0x524e9e-0x525012 est
//           gardé par `cmp [ecx+0F57Ch],0 / jz loc_525013` (EA 0x524e91/0x524e98), donc
//           n'est atteignable QUE panneau ouvert ; il ouvre UI_MsgBox_Open(dword_1822438,
//           41, StrTable005_Get(g_LangId, 1467), &String) (EA 0x524f36/0x524f46/0x524f4d)
//           = « retapez le nom pour confirmer », prompt string 1467.
//         -> clic "Oui" sur la MsgBox 41 : UI_MsgBox_OnLButtonUp 0x5C0A90 relit l'id 41
//           en [this+18h] (EA 0x5c0bba) et route le case 41 (EA 0x5c173e) vers
//           CharSelect_ReqDeleteCharByName 0x529230 (EA 0x5c1743) -> opcode 24.
//       Porté par OpenDeleteByNamePanel / CancelDeleteByNamePanel /
//       ConfirmDeleteCharByName ci-dessous.
//     - Boutons non repris (découverts, hors périmètre explicite de cette mission) :
//       "Renommer" (this[15706], nécessite un objet-ticket en inventaire, item id 1133,
//       Net_CharItemAction 0x52A9C0), "Coffre/Entrepôt" (this[15396], Net_ReqStorageList
//       0x52B730 + Net_AccountReq_op21(1,...) — un VIDAGE de case d'entrepôt, PAS une
//       suppression malgré le nom générique "AccountReq"), panneau "sélection rapide de
//       classe" (this[15703], conditionné this[15374]==40||50, semble un système
//       événementiel/bêta annexe), liste étendue GM à 10 emplacements (this[15707],
//       pagination + suppression via op21 subtype 2). Tous laissés en TODO explicite.
//
//   Écran Formulaire de création (this[15714]==2) :
//     - 4 paires de boutons +/- ajustent job (0..2), faction (0..1), visage (0..6),
//       couleur de cheveux (0..2) — job réinitialise faction/visage/couleur à 0 au
//       changement (fidèle). Une 5e paire ajuste `variant` (0..2, "sous-option" du
//       job) SANS réinitialiser les autres. SetCreateJob/Faction/Face/HairColor/Variant.
//     - Bouton "Confirmer" -> ConfirmCreateCharacter() : lit/valide le nom (host :
//       vide -> notice[38] ; ValidateNameChars/ValidateNameCharset échoue -> notice[39] ;
//       IsNameBanned/BannedWordDict::IsBanned (1432 mots, 001.DAT) échoue -> notice[40]),
//       calcule l'id de préréglage d'apparence (job,variant) -> table EXACTE
//       {5,6,7 / 11,12,13 / 17,18,19}, trouve le premier emplacement libre, envoie
//       Net_CreateCharacter (opcode 17). Succès -> retour Liste, nouveau slot
//       sélectionné, notice[41]. Échecs réseau -> notices[42(verrouille),43,701,
//       44(verrouille),45(verrouille)].
//     - Bouton "Annuler" -> CancelCreateForm() : retour Liste sans requête réseau,
//       sélection de slot INCHANGÉE (fidèle : le binaire ne touche pas this[15715]).
//
// Séquence réseau "Entrer en jeu" (déclenchée par le minuteur d'aperçu, PAS par le
// clic lui-même — cf. AdvanceEnterSequenceIfDue) :
//   Net_ReqEnterCharInfo(slot) -> code 0 : ConnectToGameServer(domainId,gamePort) ->
//     code 0 : transition prête vers EnterWorld (zoneId = celui renvoyé par le SERVEUR,
//     PAS la valeur locale dword_166A8DC — le binaire écrase la valeur locale par la
//     réponse serveur, cf. Net_ReqEnterCharInfo 0x52B070 EA 0x52A6EC-0x52A71E) ;
//     codes 1,2,6,7 : notice + Verrouillé ; codes 3,4,5 : notice + CancelEnter() ->
//     code 0 = retour Idle (nouvelle tentative possible), code 101 = notice[20] +
//     Verrouillé, AUTRE CODE NON NUL != 101 = **aucun changement d'état** (le binaire
//     ne fait RIEN dans ce cas précis — reproduit tel quel, TODO/bizarrerie d'origine).
//   code 1 (ReqEnterCharInfo lui-même en échec) : notice[55], retour Idle (PAS
//     verrouillé, nouvelle tentative possible — contrairement aux autres échecs).
//   codes 2,3,4,101,102 : notice + Verrouillé.
//
// PÉRIMÈTRE : logique de flux/décision pure. PAS de rendu (Scene_CharSelectRender est
// un pur pipeline D3D9 — device lost/reset, matrices caméra/monde, sprites, texte — dont
// le sondage des références ne révèle AUCUNE mutation d'état de flux ; TODO rendu
// explicite). PAS d'animation d'aperçu 3D précise (rotation/blend de squelette via
// PcModel_ResolveSlotAndApply, TODO visuel — seule la DURÉE avant complétion de
// l'aperçu "Entrée" est un point d'intégration fonctionnel, exposée via
// host.GetEnterPreviewDurationFrames). PAS de couplage à ts2::ui ni à
// ts2::SceneManager — tout effet de bord (réseau, UI, focus clavier, lecture des
// données de personnages) passe par CharSelectHost.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ts2::game {

// Nombre d'emplacements de personnage du flux standard (boucles `i<3` omniprésentes
// dans les 3 fonctions sources sur unk_1669394, stride 10088 octets/slot). La variante
// "10 emplacements" (dword_16692A4/this[15375]) est un overlay GM/test distinct, hors
// périmètre — cf. commentaire de tête.
// RE-VÉRIFIÉ par décompilation directe (mcp__idaTs2__decompile, session 2026-07-14,
// audit CharSelect UI/flux) : DEUX boucles `for(i=0;i<3;...)` indépendantes dans
// Scene_CharSelectUpdate 0x51BD90, TOUTES DEUX dans la branche standard (celle prise
// quand dword_16692A4==0, EA 0x51bec4 pour le calcul d'occupation initial "allSlotsFull"
// et EA 0x51c2ca pour la sélection par défaut) — aucune ambiguïté, le maximum réel du
// flux standard est bien 3, jamais davantage même dans un cas limite non modélisé.
inline constexpr int32_t kMaxCharSlots = 3;

// Sous-états internes de Scene_CharSelectUpdate (= this[1] de g_SceneMgr en scène 4).
enum class CharSelectSubState : int32_t {
    Init   = 0, // attente 30 frames puis (re)calcul de l'occupation + sélection par défaut
    Active = 1, // interactif : keepalive/30f, aperçu 3D, séquence d'entrée en jeu
    Locked = 2, // terminal — aucun traitement Update ni souris (motif "Failed" partagé
                // avec EnterWorldFlow), atteint après la plupart des erreurs réseau
};

// Écran actif dans le sous-état Actif (this[15714] d'origine, valeurs EXACTES 1/2).
enum class CharSelectScreen : int32_t {
    List       = 1, // liste des 3 emplacements + boutons Créer/Supprimer/Entrer/Quitter
    CreateForm = 2, // assistant de création (job/faction/visage/couleur + nom)
};

// Sous-état de l'aperçu 3D du personnage sélectionné, ÉCRAN LISTE UNIQUEMENT
// (this[15718] d'origine, valeurs EXACTES 1/3 — la valeur 2 existe dans le binaire pour
// un AUTRE contexte non repris ici, cf. TODO en tête de fichier).
enum class PreviewMotion : int32_t {
    Idle     = 1, // boucle d'aperçu cosmétique, aucun impact sur le flux
    Entering = 3, // armé par OnEnterButtonClicked ; au minuteur -> séquence réseau
};

// Transition de scène prête à être appliquée par l'appelant (consommée une seule fois,
// motif identique à ServerSelectFlow::transitionRequested / EnterWorldFlow).
enum class CharSelectTransition : int32_t {
    None,
    EnterWorld, // this[0]=5 d'origine ; consulter enterWorldZoneId/enterWorldSlot
};

// Un emplacement de personnage (fusion simplifiée des tableaux parallèles d'origine :
// noms this+15381.., "puissance" dword_16693B8, zone locale dword_166A8DC — cette
// dernière n'est qu'INDICATIVE, la zone AUTHENTIQUE utilisée pour EnterWorld est celle
// renvoyée par le serveur via Net_ReqEnterCharInfo, cf. EnterCharInfoResult::zoneId).
//
// SOURCE (câblée depuis la session 2026-07-14) : ces champs viennent de la fiche
// personnage brute de 10088 o (une par emplacement, persistée dans net::g_CharRecords
// par Net/Login.cpp::LoginRequest) via net::ParseCharRecord/LoadCharacterSlotsFromRecords
// (Net/CharSelectPackets.h) — layout interne RE-CONFIRMÉ par décompilation directe de
// Scene_CharSelectUpdate 0x51BD90 (EA 0x51c2f7-0x51c7d4, comparaison des adresses
// unk_1669394/dword_16693B8/dword_166A8DC/E0/E4/E8 relatives à la base unk_1669380 de
// la 1ère fiche) : nom@20 (13 o), job@36, faction@44, face@48, hairColor@52, power@56,
// lookPresetId@216, zoneId@5468, position locale x/y/z (int32 castés en float à
// l'usage, cf. binaire)@5472/5476/5480.
struct CharSlotInfo {
    bool    occupied = false; // Crt_Strcmp(name, "") != 0 — CONFIRMÉ, cf. ci-dessus
    int32_t power       = 0;  // +56 dans la fiche (dword_16693B8[2522*i]) — critère de sélection par défaut
    int32_t localZoneId = 0;  // +5468 (dword_166A8DC[2522*i]) — TODO fidélité : indicatif seulement,
                               // la zone AUTHENTIQUE vient du serveur (EnterCharInfoResult::zoneId)
    std::string name;         // +20, 13 o max (nom du personnage stocké côté serveur)
    int32_t job       = 0;    // +36
    int32_t faction    = 0;   // +44
    int32_t face       = 0;   // +48
    int32_t hairColor = 0;    // +52
    // Position de spawn LOCALE de l'aperçu 3D CharSelect (dword_166A8E0/E4/E8[2522*i],
    // stockées int32 dans le binaire puis castées en float À L'USAGE — reproduit tel
    // quel). INDICATIVE UNIQUEMENT, comme localZoneId ci-dessus : sert à positionner
    // l'aperçu du personnage sélectionné dans la scène CharSelect (flt_1675AAC/AB0/AB4,
    // EA 0x51c79e-0x51c7d4), PAS la position réelle d'apparition en jeu (celle-ci vient
    // du serveur de jeu après EnterWorld, hors périmètre de ce module).
    float   localPosX = 0.0f; // +5472
    float   localPosY = 0.0f; // +5476
    float   localPosZ = 0.0f; // +5480
};

// Formulaire de création de personnage (écran CreateForm). Champs bruts, sémantique
// exacte des valeurs job/faction/visage/couleur/variant NON CONFIRMÉE au-delà de leurs
// bornes (dword_16709DC 0..2, dword_16709E4 0..1, dword_16709E8 0..6, dword_16709EC
// 0..2, this[15716] 0..2) — TODO fidélité : lier aux tables de classes/races réelles
// quand elles seront localisées (hors périmètre de cette mission).
// PRÉCISION (re-décompilation directe de Scene_CharSelectRender 0x51CED0, session
// 2026-07-14, audit UI/flux CharSelect) : le VRAI rendu n'affiche PAS ces champs comme
// des nombres bruts — il tire un LIBELLÉ StrTable005 dynamique dépendant de la valeur :
// job (dword_16709DC) 0/1/2 -> StrTable005_Get(g_LangId, 23/24/25) EA 0x51e571-0x51e5ad ;
// faction (dword_16709E4) 0/1 -> ids 26/27 EA 0x51e614/0x51e632 ; `variant` (this[15716])
// 0/1/2, TROIS jeux d'ids SELON LE JOB -> {29,30,31} si job==0 EA 0x51e7ce-0x51e80a,
// {32,33,34} si job==1 EA 0x51e85a-0x51e896, {35,36,37} si job==2 EA 0x51e8e6-0x51e922
// (grille 3x3 en écho à ResolveLookPresetId mais avec D'AUTRES ids — vraisemblablement
// les LIBELLÉS du même préréglage plutôt que son id réseau, deux tables distinctes).
// `face` (dword_16709E8, 0..6) est affiché via `sprintf("%c %s", 'A'+face,
// StrTable005_Get(g_LangId,28))` EA 0x51e690-0x51e6f8 : lettre A-G + un mot FIXE id 28.
// CORRECTION (re-décompilation directe 0x51CED0, session 2026-07-15 — l'IDB PRIME sur les
// notes antérieures) : `hairColor` (dword_16709EC, 0..2) N'EST PAS un "swatch de couleur"
// sans texte comme le supposaient d'anciennes notes — il utilise EXACTEMENT le même
// `sprintf("%c %s", 'A'+hairColor, StrTable005_Get(g_LangId,28))` que face (MÊME mot fixe
// id 28), EA 0x51e6fd-0x51e767 ; seule la lettre change ('A'+valeur). Les CINQ champs sont
// donc TOUS du texte localisé (aucun "%d" brut dans le binaire), résolus par les helpers
// CreateJobLabelStrId / CreateFactionLabelStrId / CreateVariantLabelStrId /
// CreateFaceHairLabelLetter + kCreateFaceHairLabelWordStrId déclarés sous la struct (mapping
// bit-exact re-vérifié EA par EA). RÔLE de `variant` ÉLUCIDÉ : "sous-option" du job dont le
// LIBELLÉ (ids 29-37, grille 3x3 par (job,variant)) est DISTINCT de l'id RÉSEAU du préréglage
// (ids 5-19, ResolveLookPresetId) — deux grilles 3x3 indépendantes, l'une d'affichage, l'autre
// de protocole. INTÉGRATION (UI/LoginScene.cpp::CreateFormRender, HORS de mon périmètre
// d'édition) : ce rendu doit appeler ces helpers (game::Str(id) pour job/faction/variant ;
// format "%c %s" pour face/hairColor) pour rester fidèle. Le MODÈLE de données
// CharCreateForm ci-dessous reste correct dans tous les cas.
struct CharCreateForm {
    int32_t job       = 0; // dword_16709DC, 0..2
    int32_t faction    = 0; // dword_16709E4, 0..1
    int32_t face       = 0; // dword_16709E8, 0..6
    int32_t hairColor = 0; // dword_16709EC, 0..2
    int32_t variant    = 0; // this[15716], 0..2 ("sous-option" du job)
    std::string name;       // lu depuis la zone de saisie par host.GetEditedName()
};

// --- Helpers de LIBELLÉS du formulaire de création (Scene_CharSelectRender 0x51CED0) ---
// Mapping bit-exact re-décompilé EA par EA (session 2026-07-15). Ces fonctions exposent le
// VRAI mécanisme d'affichage pour que le rendu (UI/LoginScene.cpp::CreateFormRender) montre
// du texte localisé fidèle au lieu des entiers bruts "%d" actuels. Elles sont PURES (aucun
// effet de bord) : l'appelant récupère l'id puis appelle sa propre résolution StrTable005
// (p.ex. game::Str(id), déjà utilisé par LoginScene pour job/faction).

// job (dword_16709DC 0..2) -> StrTable005_Get(g_LangId, 23/24/25). EA 0x51e548-0x51e5c9.
inline int32_t CreateJobLabelStrId(int32_t job) { return 23 + job; }

// faction (dword_16709E4 0..1) -> StrTable005_Get(g_LangId, 26/27). EA 0x51e60b-0x51e64e.
inline int32_t CreateFactionLabelStrId(int32_t faction) { return 26 + faction; }

// variant (this[15716] 0..2) -> grille 3x3 par (job, variant) : job 0 -> {29,30,31},
// job 1 -> {32,33,34}, job 2 -> {35,36,37}. EA 0x51e76c-0x51e93e. ÉLUCIDE le rôle de
// `variant` : c'est une sous-option du job dont le LIBELLÉ (29-37) est distinct de l'id
// RÉSEAU du préréglage (5-19, ResolveLookPresetId) — deux grilles 3x3 indépendantes.
inline int32_t CreateVariantLabelStrId(int32_t job, int32_t variant) { return 29 + 3 * job + variant; }

// face (dword_16709E8 0..6) ET hairColor (dword_16709EC 0..2) partagent EXACTEMENT le même
// format d'affichage : sprintf("%c %s", 'A'+valeur, StrTable005_Get(g_LangId, 28)).
// EA face 0x51e690-0x51e6f8, EA hairColor 0x51e6fd-0x51e767. Le mot fixe (id 28) est PARTAGÉ
// par les deux champs ; seule la lettre change ('A'+valeur -> face A-G, hairColor A-C).
inline constexpr int32_t kCreateFaceHairLabelWordStrId = 28;
inline char CreateFaceHairLabelLetter(int32_t value) { return static_cast<char>('A' + value); }

// Résultat de Net_ReqEnterCharInfo (opcode 22) — 3 dwords out-params dans le binaire.
struct EnterCharInfoResult {
    int32_t resultCode = 101; // 0=ok, 1/2/6/7=échec verrouillant, 3/4/5=échec annulable,
                               // 101/102=échec réseau bas niveau (timeout/déconnexion)
    int32_t domainId   = 0;   // v27 d'origine -> Net_SelectServerDomain
    int32_t gamePort   = 0;   // v22 d'origine -> Net_ConnectGameServer
    int32_t zoneId      = 0;   // AUTHENTIQUE (écrase la valeur locale du slot)
};

// --- Points d'intégration (réseau/UI/données de personnages), nullptr = no-op sûr ---
struct CharSelectHost {
    // Peuple `slots` (occupation/nom/job/faction/visage/couleur/puissance/zone locale)
    // à l'entrée en scène. Le binaire lit directement unk_1669394/dword_16693B8/
    // dword_166A8DC.. ; câblé depuis la session 2026-07-14 sur
    // net::LoadCharacterSlotsFromRecords (Net/CharSelectPackets.h), qui parse les 3
    // fiches persistées par Net/Login.cpp::LoginRequest (net::g_CharRecords) — cf.
    // CharSlotInfo ci-dessus pour le détail des offsets. nullptr => slots inchangés.
    std::function<void(std::array<CharSlotInfo, kMaxCharSlots>&)> LoadCharacterSlots;

    // Net_AccountKeepAlive 0x5298F0 (opcode 12). Retourne le code brut ; 101 = session
    // expirée. nullptr => traité comme "vivant" (code != 101).
    std::function<int32_t()> AccountKeepAlive;

    // Str_ValidateNameChars(nom du slot) 0x53FD70 — précondition de "Entrer en jeu"
    // (nom du personnage STOCKÉ, pas une saisie utilisateur). nullptr => true.
    std::function<bool(int32_t slotIndex)> IsCharacterNameValid;

    // g_GmAuthLevel >= 1 0x1669294 — contourne IsCharacterNameValid pour "Entrer en jeu".
    std::function<bool()> HasGmAuthLevel;

    // UI_NoticeDlg_Open(_, StrTable005_Get(g_LangId, strId), "") 0x5C0280, en ignorant
    // le paramètre "type" (1/2) qui ne change pas le flux ici (TODO rendu).
    std::function<void(int32_t strId)> ShowNotice;

    // UI_MsgBox_Open(2, StrTable005_Get(g_LangId,49), "") : ouvre la confirmation
    // Oui/Non de suppression. L'appelant DOIT rappeler ConfirmDeleteCharacter() sur
    // clic "Oui" (le binaire route cela via UI_MsgBox_OnLButtonUp 0x5C0A90 ->
    // CharSelect_ReqDeleteChar 0x528FD0, hors périmètre du composant MsgBox générique).
    std::function<void()> ShowDeleteConfirm;

    // Str_ValidateNameChars(nomSaisi) 0x53FD70 — validation du nom saisi en création.
    // Implémentation FIDÈLE fournie par ValidateNameCharset() ci-dessous (déclarée
    // pour que l'hôte n'ait qu'à la brancher : `ValidateNameChars = &ValidateNameCharset;`
    // ou une lambda équivalente). nullptr => true (aucune restriction, non fidèle,
    // uniquement pour des hôtes de test).
    std::function<bool(const std::string&)> ValidateNameChars;

    // maybe_Dict001_MatchWord(g_BannedWordList, nomSaisi) 0x4C1410 — filtre de mots
    // bannis (001.DAT). true = nom banni/rejeté. L'hôte doit brancher
    // `game::g_Strings.bannedWords.IsBanned(nomSaisi)` (StringTables.h, 1432 mots
    // CP949 réellement chargés) — cf. BannedWordDict::IsBanned pour l'écart documenté
    // (sûr mais pas bit-exact vis-à-vis de sub_4C1410). nullptr => false (aucun mot
    // banni, non fidèle, uniquement pour des hôtes de test).
    std::function<bool(const std::string&)> IsNameBanned;

    // GetWindowTextA(zone de saisie du nom) — lit le nom actuellement tapé par le
    // joueur dans le formulaire de création.
    std::function<std::string()> GetEditedName;

    // Rng_Next() % 3 0x7603FD — job initial aléatoire à l'ouverture du formulaire.
    // nullptr => 0.
    std::function<int32_t()> RandomInitialJob;

    // Net_CreateCharacter(slot, formulaire+preset, &code) 0x52A4A0 (opcode 17).
    std::function<int32_t(int32_t slotIndex, const CharCreateForm& form, int32_t lookPresetId)> CreateCharacter;

    // Net_CharSlotAction(slot,1,0,&code) 0x52A740 (opcode 18, action=1=suppression),
    // via CharSelect_ReqDeleteChar 0x528FD0 (EA 0x528fee).
    std::function<int32_t(int32_t slotIndex)> DeleteCharacter;

    // Net_CharSlotAction(slot,2,listIndex,&code) 0x52A740 (opcode 18, action=2=
    // restauration), via CharSelect_ReqRestoreChar 0x5295D0 (EA 0x5295f6 :
    // `Net_CharSlotAction(*(this+15715), 2, *(this+15704), &v18)`). `listIndex` = champ
    // +0xF560, cf. CharSelectState::restoreListIndex. Le builder net::CharSlotAction
    // expose DÉJÀ action et arg — rien à changer côté Net/CharSelectPackets.cpp.
    // CONSOMMÉ désormais par game::ConfirmRestoreCharacter (le C++-side caller existe).
    // TODO CÂBLAGE [ancre 0x5295f6] : reste à (a) brancher ce hook dans
    // UI/LoginScene.cpp::BuildCharSelectHost sur
    // `net::CharSlotAction(net_->Client(), slot, 2, listIndex)`, (b) appeler
    // game::ConfirmRestoreCharacter sur le clic "Oui" de restauration, (c) modéliser la
    // liste de restauration qui alimente restoreListIndex — HORS du périmètre de ce front.
    std::function<int32_t(int32_t slotIndex, int32_t listIndex)> RestoreCharacter;

    // Net_ReqVerifyCharName(slotEnc, name, &code) 0x52B4C0 (opcode 24), via
    // CharSelect_ReqDeleteCharByName 0x529230 (EA 0x5292cd). `slotEnc` est ENCODÉ
    // (slot + 100*flag), cf. ConfirmDeleteCharByName.
    // CONSOMMÉ désormais par game::ConfirmDeleteCharByName (le C++-side caller existe).
    // TODO CÂBLAGE [ancre 0x5292cd] : reste à brancher ce hook sur net::VerifyCharName
    // (Net/CharSelectPackets.h) dans UI/LoginScene.cpp::BuildCharSelectHost, et à appeler
    // OpenDeleteByNamePanel/ConfirmDeleteCharByName depuis les clics UI — HORS du périmètre.
    std::function<int32_t(int32_t slotEnc, const std::string& name)> VerifyCharName;

    // GetWindowTextA(dword_166900C, String, 49) 0x529273 — nom retapé dans la zone de
    // saisie DU PANNEAU de suppression par nom. ATTENTION : c'est une zone EDIT
    // DIFFÉRENTE de celle du formulaire de création (GetEditedName ci-dessus lit
    // dword_1668FCC sur 13 o, EA 0x526581-0x52658f ; celle-ci lit dword_166900C sur
    // 49 o) — les deux hooks ne sont PAS interchangeables. nullptr => chaîne vide
    // (= notice[1463] sans envoi, cf. ConfirmDeleteCharByName).
    std::function<std::string()> GetDeleteByNameInput;

    // UI_FocusEditBox(&g_UIEditBoxMgr, index) 0x50F4A0 — donne le focus à l'une des 21
    // zones EDIT natives (index 0 = retour au jeu / aucune saisie active). Le flux
    // n'utilise que deux index PROUVÉS : 19 à l'ouverture du panneau (`push 13h`,
    // EA 0x525fcc/0x525fd3) et 0 au succès de l'opcode 24 (EA 0x529365).
    std::function<void(int32_t editBoxIndex)> FocusEditBox;

    // SetWindowTextA(dword_166900C, "") 0x525fe3 — vide la zone de saisie du panneau de
    // suppression par nom à son ouverture.
    std::function<void()> ClearDeleteByNameInput;

    // Publie l'identité du personnage choisi (élément/fiche brute) dans l'état monde
    // AVANT le handshake du serveur de jeu. Miroir du memcpy unique de
    // Scene_CharSelectUpdate EA 0x51c6e7-0x51c707 :
    //   Crt_Memcpy(&g_SelfCharInvBlock /*0x1673170*/,
    //              &unk_1669380 + 10088 * this[+0xF58C] /*selectedSlot*/, 0x2768u)
    // Ce memcpy de 10088 o pose À LA FOIS le bloc d'inventaire ET l'« élément »
    // (g_LocalElement = dword_1673194 = 0x1673170 + 0x24 -> fiche[+36] = le champ `job`),
    // qui part ensuite sur le fil à l'offset [137..140] du paquet d'auth op11 de
    // Net_ConnectGameServer 0x462A70 (EA 0x462d5d). ORDRE PROUVÉ : 0x51c6e7 (memcpy)
    // ≺ 0x51c81d (Net_ReqEnterCharInfo) ≺ 0x51c850 (Net_SelectServerDomain) ≺
    // Net_ConnectGameServer — d'où l'appel en TÊTE de FireEnterSequence.
    // INVOQUÉ désormais en TÊTE de FireEnterSequence (Game/CharSelectFlow.cpp), avant
    // host.RequestEnterCharInfo — ordre fidèle à 0x51c6e7 ≺ 0x51c81d.
    // TODO CÂBLAGE [ancre 0x51c6e7] : ce module est volontairement découplé de
    // game::g_World (aucun accès direct — tout passe par cet hôte). Le câblage réel
    // (`g_World.self.element = slots[slot].job;` + peuplement de
    // `g_World.self.charInvBlock` depuis net::g_CharRecords[slot]) vit dans
    // UI/LoginScene.cpp, HORS du périmètre d'édition de ce front. TANT QUE CE HOOK N'EST
    // PAS CÂBLÉ (l'hôte ne pose pas encore la lambda), le champ [137..140] du handshake
    // reste à 0 (défaut GAMEAUTH_Element_Zero NON CLOS côté LoginScene).
    std::function<void(int32_t slotIndex)> PublishSelfFromSlot;

    // Net_ReqEnterCharInfo(slot,&domainId,&port,&zoneId,&code) 0x52B070 (opcode 22).
    std::function<EnterCharInfoResult(int32_t slotIndex)> RequestEnterCharInfo;

    // Net_SelectServerDomain(domainId,&host) 0x53FE90 + Net_ConnectGameServer(client,
    // host,port,&code) 0x462A70, repliés en un seul point d'intégration réseau.
    std::function<int32_t(int32_t domainId, int32_t gamePort)> ConnectToGameServer;

    // Net_ReqCancelEnter(&code) 0x52B310 — annule une entrée après échec de connexion
    // récupérable (codes 3/4/5 de ConnectToGameServer).
    std::function<int32_t()> CancelEnter;

    // PcModel_ResolveSlotAndApply(...) 0x4E5A00, réduit à la SEULE information utile au
    // flux : la durée (en frames 30 FPS) de l'animation d'aperçu "Entrée" avant
    // déclenchement de la séquence réseau. nullptr => kDefaultEnterPreviewFrames.
    std::function<float(int32_t slotIndex)> GetEnterPreviewDurationFrames;

    // Net_CloseSocket(&g_NetClient) 0x463000 + g_QuitFlag=1 0x815590 — bouton "Quitter".
    std::function<void()> CloseConnectionAndQuit;
};

// --- État complet de l'écran CharSelect ---
struct CharSelectState {
    CharSelectSubState subState = CharSelectSubState::Init;
    int32_t frameCounter = 0; // this[2]

    CharSelectScreen screen = CharSelectScreen::List; // this[15714]

    std::array<CharSlotInfo, kMaxCharSlots> slots{};
    bool    allSlotsFull  = false; // this[15705]
    int32_t selectedSlot  = -1;    // this[15715], -1 = aucun

    // Aperçu 3D (écran Liste uniquement) — cf. PreviewMotion.
    PreviewMotion previewMotion   = PreviewMotion::Idle; // this[15718]
    float         previewElapsed  = 0.0f;                 // this[15719], en "frames" (a2*30)
    bool          enterSequenceFired = false; // garde d'origine = g_MorphInProgress (simplifiée
                                               // en un flag one-shot local, cf. TODO ci-dessous)

    // --- Panneau "suppression par saisie du nom" (opcode 24) ---
    // Les deux champs d'origine sont ARMÉS ET REMIS À ZÉRO ENSEMBLE, sans exception :
    // les 12 seules références de +0xF57C/+0xF580 de toute l'image jeu ([0x401000,
    // 0x6d7234)) forment 3 paires d'écriture rectilignes (0->0 : EA 0x51c223/0x51c230
    // en Init, EA 0x524ff4/0x525004 à l'annulation, EA 0x52939e/0x5293ae au succès
    // op24 ; 1->1 : EA 0x52601d/0x52602d, UNIQUE site d'armement) + 4 lectures
    // (0x520cc7/0x5224c6/0x524e91 pour +0xF57C, 0x5292b1 pour +0xF580).
    // L'invariant `panneau ouvert => listFlag == 1` est donc STRUCTUREL — il fixe la
    // valeur de slotEnc, cf. ConfirmDeleteCharByName.
    bool    deleteByNamePanelOpen = false; // this[15711] / +0xF57C
    int32_t deleteByNameListFlag  = 0;     // this[15712] / +0xF580 (multiplicateur ×100)

    // Index de sélection dans la liste de restauration (this[15704] / +0xF560), envoyé
    // comme `arg` de l'opcode 18 action=2 (EA 0x5295f6). Initialisé à -1 par le binaire
    // (EA 0x51c1e2 : `mov dword ptr [ecx+0F560h], 0FFFFFFFFh`) = aucune sélection.
    // TODO [ancres 0x524232-0x524250 / 0x5242ac-0x5242d8 / 0x5242bb] : la LISTE de
    // restauration elle-même n'est PAS modélisée (contenu, nombre d'entrées = champ
    // +0xF3C8, boutons flèche précédent/suivant qui clampent cet index dans
    // [0, count-1], rendu EA 0x52030f/0x52044f). Tant que cette liste n'est pas portée,
    // ce champ reste à sa valeur d'init et ConfirmRestoreCharacter n'a pas d'appelant.
    int32_t restoreListIndex = -1; // this[15704] / +0xF560

    // Formulaire de création (écran CreateForm).
    CharCreateForm createForm{};

    // Transition consommable une seule fois par UpdateCharSelect().
    CharSelectTransition pendingTransition = CharSelectTransition::None;
    int32_t enterWorldZoneId = 0;
    int32_t enterWorldSlot   = -1;
};

// Str_ValidateNameChars 0x53FD70 — reproduction FIDÈLE (décompilée via idaTs2,
// server_health/decompile, IDB `RE/TwelveSky2.exe.i64`). L'original :
//   1. MultiByteToWideChar(CP_ACP=0, 0, name, -1, buf, 13) — buffer FIXE de 13
//      WCHAR (12 caractères utiles + NUL) : échoue (false) si la conversion rate,
//      ce qui inclut le cas "nom trop long pour tenir dans 13 WCHAR" (le binaire
//      n'a AUCUNE autre garde de longueur — c'est cette capacité de buffer qui
//      fait à la fois office d'encodage ET de borne dure à 12 caractères).
//   2. sub_760F03 = wcslen(buf) (confirmé par décompilation, boucle NUL-scan
//      triviale).
//   3. CHAQUE caractère large doit être dans l'un des 4 intervalles EXACTS :
//      '0'-'9' (0x30-0x39), 'A'-'Z' (0x41-0x5A), 'a'-'z' (0x61-0x7A), ou le bloc
//      thaï U+0E00-U+0E7F (présent tel quel dans le binaire EU — reproduit sans
//      interprétation, potentiellement un vestige d'un build partagé multi-région).
//      Un seul caractère hors de ces plages -> false immédiat.
// ÉCART DE COMPORTEMENT ASSUMÉ (fidèle, pas un bug de portage) : un nom VIDE (0
// caractère) traverse la boucle sans itération et renvoie TRUE dans le binaire —
// ce n'est PAS cette fonction qui rejette les noms vides. C'est l'appelant
// (Scene_CharSelectOnMouseUp, EA 0x52658f-0x5265b7) qui rejette le vide EN AMONT
// via `GetWindowTextA(...) == 0` -> notice[38], AVANT même d'appeler
// Str_ValidateNameChars ; caractères invalides -> notice[39] (EA 0x5265c6-0x5265ed) ;
// mot banni -> notice[40] (EA 0x5265fc-0x526623). ConfirmCreateCharacter()
// reproduit cet ordre exact des 3 notices distinctes.
bool ValidateNameCharset(const std::string& name);

// Scene_CharSelectUpdate 0x51BD90. Appeler 1x/frame (30 FPS) tant que la scène active
// est CharSelect. Retourne la transition prête (None la plupart du temps) — à consommer
// par l'appelant exactement comme ServerSelectFlow/EnterWorldFlow.
CharSelectTransition UpdateCharSelect(CharSelectState& state, const CharSelectHost& host, float dt);

// Clic sur un emplacement occupé de la liste (Scene_CharSelectOnMouseDown, boucle sur
// les 10 icônes -> ici réduite aux kMaxCharSlots occupés du flux standard). No-op si
// l'emplacement est vide, hors bornes, ou si `slotIndex` est déjà la sélection.
void SelectCharacterSlot(CharSelectState& state, int32_t slotIndex);

// Bouton "Créer" (écran Liste). Ouvre le formulaire si un emplacement est libre
// (notice[22] "liste pleine" sinon) ; job initial tiré via host.RandomInitialJob.
void OnCreateButtonClicked(CharSelectState& state, const CharSelectHost& host);

// Paires de boutons +/- du formulaire de création. `delta` = -1 ou +1 (fidèle aux
// bornes EXACTES [0,2]/[0,1]/[0,6]/[0,2]/[0,2] du binaire, clampé — PAS de rebouclage).
// SetCreateJob réinitialise faction/visage/couleur à 0 (fidèle) ; les autres n'affectent
// que leur propre champ.
void SetCreateJob(CharSelectState& state, int32_t delta);
void SetCreateFaction(CharSelectState& state, int32_t delta);
void SetCreateFace(CharSelectState& state, int32_t delta);
void SetCreateHairColor(CharSelectState& state, int32_t delta);
void SetCreateVariant(CharSelectState& state, int32_t delta);

// Bouton "Confirmer" du formulaire de création. Valide le nom dans l'ORDRE EXACT du
// binaire (EA 0x52658f-0x526623) : vide -> notice[38] ; caractères refusés
// (host.ValidateNameChars, cf. ValidateNameCharset) -> notice[39] ; mot banni
// (host.IsNameBanned, cf. BannedWordDict::IsBanned) -> notice[40]. Calcule ensuite
// l'id de préréglage EXACT (job,variant) et envoie Net_CreateCharacter au premier
// emplacement libre. Gère tous les codes retour d'origine (0/1/2/3/0x65/0x66) ;
// retourne true si un personnage a été créé (état déjà remis en écran Liste, slot
// sélectionné).
bool ConfirmCreateCharacter(CharSelectState& state, const CharSelectHost& host);

// Bouton "Annuler" du formulaire de création. Retour à l'écran Liste, aperçu remis à
// Idle, sélection de slot INCHANGÉE (fidèle). Aucune requête réseau.
void CancelCreateForm(CharSelectState& state);

// Bouton "Supprimer" (écran Liste). Précondition : un emplacement est sélectionné et
// l'aperçu n'est pas en cours d'entrée (PreviewMotion::Entering). Ouvre la confirmation
// via host.ShowDeleteConfirm() — n'envoie AUCUNE requête réseau elle-même. Retourne
// false si les préconditions ne sont pas remplies (notice[47] "aucune sélection" via
// host.ShowNotice, comme le binaire — sauf le cas région this[15374]==60, non modélisé).
bool OnDeleteButtonClicked(CharSelectState& state, const CharSelectHost& host);

// À appeler par l'hôte sur clic "Oui" de la confirmation ouverte par
// OnDeleteButtonClicked(). Envoie Net_CharSlotAction(slot,1,0,&code) (opcode 18) et
// applique le résultat (succès -> emplacement libéré + désélection + notice[50] ;
// échecs -> notices[51(verrouille),411,48,633,2091,52(verrouille),53(verrouille)]).
void ConfirmDeleteCharacter(CharSelectState& state, const CharSelectHost& host);

// --- Panneau "suppression par saisie du nom" (opcode 24, Net_ReqVerifyCharName 0x52B4C0) ---
// Second mécanisme de suppression, à DOUBLE confirmation (retaper le nom du personnage),
// distinct de l'opcode 18 action=1 — cf. l'élucidation complète en tête de fichier.

// Ouvre le panneau (Scene_CharSelectOnMouseUp, EA 0x525fc0-0x52602d) : vide+focus la zone
// de saisie (host.ClearDeleteByNameInput / host.FocusEditBox(19)) et arme CONJOINTEMENT
// deleteByNamePanelOpen ET deleteByNameListFlag à 1 (bloc rectiligne 0x52601d/0x52602d) —
// c'est cet armement qui fixe l'invariant listFlag==1 dont dépend slotEnc. No-op hors
// sous-état Actif / écran Liste. Gardes d'éligibilité UI non modélisées (TODO ancré).
void OpenDeleteByNamePanel(CharSelectState& state, const CharSelectHost& host);

// Ferme le panneau SANS envoi réseau : remet les deux drapeaux à 0 (EA 0x524ff4/0x525004).
void CancelDeleteByNamePanel(CharSelectState& state);

// À appeler par l'hôte sur clic "Oui" de la MsgBox 41 (CharSelect_ReqDeleteCharByName
// 0x529230). Lit le nom retapé (host.GetDeleteByNameInput) : VIDE -> notice[1463] SANS
// envoi (EA 0x52928c) ; sinon envoie l'opcode 24 (host.VerifyCharName) avec
// slotEnc = (uint8_t)selectedSlot + 100*(uint8_t)deleteByNameListFlag (EA 0x5292cd) et
// route les codes 0/1/2/3/4/5/101/102/default (notices 1464/1465/1468/1469/1466/1470/703/
// 704, EA 0x5292f5-0x529535 ; succès -> désélection + fermeture du panneau ; 101/102 ->
// verrou ; default non modélisé, TODO ancré).
void ConfirmDeleteCharByName(CharSelectState& state, const CharSelectHost& host);

// À appeler par l'hôte sur clic "Oui" de la confirmation de RESTAURATION
// (CharSelect_ReqRestoreChar 0x5295D0). Envoie l'opcode 18 action=2 via host.RestoreCharacter
// (slot=selectedSlot, arg=restoreListIndex, EA 0x5295f6) et applique le résultat (succès
// 0 -> désélection + notice[1271] ; 1/101/102 -> notice[51/52/53] + verrou ; 2/5/11..15 ->
// notices[1272/2091/2541..2545], EA 0x529615-0x529845). SANS appelant tant que la liste de
// restauration n'est pas portée (cf. CharSelectState::restoreListIndex) et que
// host.RestoreCharacter n'est pas câblé dans UI/LoginScene.cpp (HORS périmètre).
void ConfirmRestoreCharacter(CharSelectState& state, const CharSelectHost& host);

// Bouton "Entrer en jeu" (écran Liste). Précondition : un emplacement sélectionné, pas
// déjà en cours d'entrée, et (nom du perso stocké valide OU autorisation GM). Sur
// succès, arme PreviewMotion::Entering (la séquence réseau se déclenche au minuteur
// suivant dans UpdateCharSelect, PAS ici — fidèle : ce n'est qu'au clic que le binaire
// arme this[15718]=3, jamais synchrone). Retourne false sinon (notice[1856] nom
// invalide, ou notice[47] aucune sélection).
bool OnEnterButtonClicked(CharSelectState& state, const CharSelectHost& host);

// Bouton "Quitter" (écran Liste). No-op si en cours d'entrée. Appelle
// host.CloseConnectionAndQuit() (Net_CloseSocket + g_QuitFlag=1) — la fermeture réelle
// de l'application reste hors périmètre de ce module (boucle App_FrameTick).
void OnQuitButtonClicked(CharSelectState& state, const CharSelectHost& host);

} // namespace ts2::game

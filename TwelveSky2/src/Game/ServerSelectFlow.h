// Game/ServerSelectFlow.h — flux d'état de l'écran de sélection de serveur (scène 2).
//
// Réécriture fidèle du FLUX/ÉTAT de :
//   Scene_ServerSelectUpdate        0x518B30  (~1.8 Ko, machine à 2 sous-états)
//   Scene_ServerSelectOnMouseDown   0x519780  (clic groupe / clic serveur)
//   Scene_ServerSelectOnMouseUp     0x519AC0  (bouton d'action -> confirmation retour, hors périmètre ici)
//   Net_ServerStatusThread          0x518AB0  (thread worker : population par serveur)
//   Net_QueryServerStatus           0x519CC0  (ping/population d'un serveur)
//   Cfg_SaveLastServer              0x519C40  (persistance du dernier choix)
// tel que décompilé et déjà documenté dans Docs/TS2_CLIENT_SHELL.md §2.10 ("Flux de
// connexion & sélection serveur/personnage") et RE/shell_findings.json (clé "login_flow").
// L'IDB (idaTs2, RE/TwelveSky2.exe.i64) reste la source de vérité ; ce module reprend
// fidèlement le flux qui y est déjà extrait faute d'accès direct à l'IDA MCP dans cette
// session (idaTs2 non connecté ici) — aucune donnée n'est inventée, tout écart/incertitude
// hérité de la doc source est reporté en TODO ci-dessous.
//
// FLUX D'ORIGINE (fidèle au binaire, cf. g_SceneMgr @0x1676180 : [+0]=scene_id,
// [+4]=sous-état, [+8]=compteur de frames) :
//
//   Sous-état 0 (Init) : incrémente le compteur de frames ; à 30 frames (0x1E) :
//     - remet l'UI à plat (TODO rendu : UI_ResetAllDialogs 0x5AC3F0)
//     - charge la BGM Z000.BGM en mode 3 (TODO audio : Snd_LoadOggToBuffers)
//     - tire un id de sprite de FOND aléatoire (this[168] = 2380 ou 2381, Rng_Next()%2,
//       EA 0x518c29) -> fichiers 001_02381.IMG / 001_02382.IMG
//     - construit la table de serveurs selon g_ServerListMode/dword_166918C
//       (0/1/2 -> liste mono-serveur "12sky2-login.geniusorc.com" port 8088 ; sinon ->
//       liste multi-canaux ports 10005/10205/10305/11096/11095/11092 — ORDRE EXACT du
//       binaire, 11096 AVANT 11095, EA 0x518fac-0x519164)
//     - mélange l'ordre d'affichage des groupes (this[170..], TODO RNG fidèle)
//     - lance CreateThread(Net_ServerStatusThread) -> passe en sous-état 1. Le thread
//       interroge les serveurs d'indice [this[15372]..this[15373]] (= dword_16851B0..B4,
//       les MÊMES mots que selectedGroupBtnLo/Hi) et remplit maxPopulation/loadStep/
//       currentPopulation de chacun depuis un record de statut de 17 octets.
//   Sous-état 1 (Idle) : attend un clic ; le thread de statut alimente en tâche de
//     fond les populations (server_status[i], -1 = encore en cours d'interrogation).
//     Heartbeat anti-triche (sub_6DE3F7, toutes les 30 frames) IGNORÉ ICI — hors
//     périmètre de cette mission (cf. CLAUDE.md : ne pas reverse l'anticheat).
//   Clic sur un groupe/canal : OnGroupClicked -> sel_group=i, relance l'interrogation
//     de statut du groupe, persiste le choix (Cfg_SaveLastServer).
//   Clic sur un serveur : OnServerClicked -> si population connue (>=0) ET < capacité
//     max -> sel_server=i, TRANSITION prête vers Login (scene_id=3) ; sinon (serveur
//     plein ou encore inconnu) aucun effet, exactement comme le binaire.
//   Relâchement sur le bouton d'action (OnMouseUp 0x519AC0) : ouvre une confirmation
//     modale de retour arrière — pas de logique de flux d'état ici (TODO UI).
//
// PÉRIMÈTRE : logique de flux/état pure — PAS de rendu de la liste visuelle (sprites,
// libellés localisés StrTable, layout des rangées/boutons -> TODO précis marqués
// ci-dessous), PAS de vrai threading/réseau (le "thread" de statut est modélisé comme
// un point d'intégration synchrone injectable, PollServerStatuses(), à appeler
// périodiquement par l'appelant, qui peut le brancher sur un vrai thread s'il veut
// reproduire l'asynchronisme exact), PAS de couplage direct à ts2::SceneManager
// (l'appelant lit UpdateServerSelect()/OnServerClicked() et effectue lui-même la
// transition ts2::Scene::Login).
//
// Autonomie : ce module n'inclut QUE la STL — ServerSelect ne touche à aucun champ de
// GameState.h / ClientRuntime.h / AutoPlaySystem.h (pas de joueur/monde impliqué ici).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ts2::game {

// Mode de construction de la liste de serveurs (dword_166918C d'origine, également
// utilisé par le bootstrap GameGuard pour choisir "TwelveSky2EU" vs "TwelveSky2EUTest").
// TODO fidélité : la valeur exacte pilotant ce mode en jeu réel (0/1/2 vs autre) n'a pas
// été confirmée dynamiquement (cf. open_questions de shell_findings.json) ; les DEUX
// branches sont reproduites, le choix de la branche active reste au soin de l'appelant.
//
// PRÉCISION (réaudit UI/LoginScene.cpp du 2026-07-14, re-décompilation intégrale de
// WinMain 0x4609C0 + Scene_ServerSelectUpdate 0x518B30 + Scene_ServerSelectRender
// 0x519250) : SingleServer N'EST PAS une branche secondaire/anecdotique. WinMain
// (EA 0x4609F1 puis 0x460BAE) fixe dword_166918C au 1er jeton `/`-délimité de la ligne
// de commande (Crt_Atoi) ; avec la commande de lancement documentée par ce projet
// (CLAUDE.md : `TwelveSky2.exe /0/0/2/1024/768`), ce jeton vaut "0" -> SingleServer.
// De plus, Scene_ServerSelectRender ne dessine une grille de PLUSIEURS boutons serveur
// QUE lorsque dword_166918C==0 (EA 0x5194CB `if (g_ServerModeFlag)` bascule sinon vers
// un rendu "nombre unique", UI_DrawNumberValue, qui ne reflète QUE le serveur d'indice
// 0) — donc même quand MultiChannel est actif, ses 5 canaux au-delà de l'indice 0 ne
// sont jamais rendus comme boutons cliquables par cette fonction. Consommateur actuel :
// UI/LoginScene.cpp n'appelle PAS ce module (BuildServerList() ci-dessous est du code
// mort du point de vue de LoginScene, qui a sa propre construction de liste locale) —
// voir le commentaire détaillé dans LoginScene.cpp::BuildServerList()/ServerSelectRender().
enum class ServerListMode : int32_t {
    SingleServer = 0, // g_ServerListMode == 0, 1 ou 2 -> liste mono-serveur, port fixe 8088
    MultiChannel = 1, // toute autre valeur -> liste multi-canaux (6 ports observés)
};

// Sous-états internes de Scene_ServerSelectUpdate (= this[1] de g_SceneMgr en scène 2).
enum class ServerSelectSubState : int32_t {
    Init = 0, // attente 30 frames puis (re)construction de la liste + lancement du thread de statut
    Idle = 1, // liste construite, thread de statut actif en tâche de fond, en attente de clic
};

// Une entrée de la table de serveurs. Le binaire garde 4 tableaux parallèles
// (server_names this[371], server_ports this[10371], server_maxpop this[12371],
// server_status this[14371]) ; fusionnés ici en un seul enregistrement par lisibilité.
struct ServerEntry {
    std::string name;                   // this[371]  : nom d'hôte / libellé (stride 40 o dans le binaire, cf. Net_ServerStatusThread &g_ServerNameTable[40*i])
    uint16_t    port = 0;               // this[10371]: port de connexion
    int32_t     maxPopulation = 0;      // this[12371]: capacité max — ALIMENTÉE par le serveur (record de statut octets 5-8, Net_QueryServerStatus 0x519CC0). 0 avant réception.
    int32_t     loadStep = 0;           // this[13371]: pas de population par palier de barre de charge — ALIMENTÉ par le serveur (record de statut octets 9-12). 0 avant réception.
    int32_t     currentPopulation = -1; // this[14371]: population courante (record de statut octets 13-16 = valeur de retour) ; -1 = interrogation en cours/échec
    int32_t     groupIndex = 0;         // groupe auquel appartient ce serveur (this[169] groupes, 5 serveurs-boutons/groupe : boutons 5*g..5*g+4)
};

// État complet de l'écran ServerSelect (mirroir de la portion pertinente de
// g_SceneMgr @0x1676180 — cf. Docs/TS2_CLIENT_SHELL.md pour les offsets réels du binaire).
struct ServerSelectState {
    ServerSelectSubState subState = ServerSelectSubState::Init;
    int32_t frameCounter = 0;          // this[2] : compteur de frames du sous-état courant

    bool    listBuilt = false;         // vrai après la construction initiale de la table
    bool    statusThreadActive = false;// vrai tant que l'interrogation de statut est censée tourner (TODO réseau réel)
    int32_t backgroundImageId = 0;     // this[168] : id de sprite de fond, tiré aléatoirement 2380/2381 (Rng_Next()%2, EA 0x518c29)

    int32_t groupCount = 0;                // this[169] : nombre de GROUPES ; le binaire le fixe à 1 dans TOUS les modes (EA 0x518cff/0x518e0b/0x518f6e)
    std::vector<int32_t> groupOrder;       // this[170..] : ordre d'affichage mélangé des groupes

    std::vector<ServerEntry> servers;      // table fusionnée (voir ServerEntry)

    int32_t selectedGroup = -1;        // this[15371] : groupe actuellement affiché/mis en avant (0 après construction)
    int32_t selectedGroupBtnLo = -1;   // this[15372] : borne basse d'index bouton (== dword_16851B0, plage du thread de statut). Clic groupe -> 5*group (EA 0x5198ba)
    int32_t selectedGroupBtnHi = -1;   // this[15373] : borne haute d'index bouton (== dword_16851B4). Clic groupe -> 5*group+4 pour groupes 0/1 (EA 0x519904)
    int32_t selectedServer = -1;       // this[15374] : serveur validé (lu par Login pour se connecter)

    // Bookkeeping SYNTHÉTIQUE (absent du binaire) : le binaire écrit directement
    // g_SceneMgr[0]=3 depuis OnMouseDown, sans repasser par Update. Pour exposer une
    // API UpdateServerSelect()->bool découplée de SceneManager (cf. mission), on arme
    // ce flag dans OnServerClicked() et UpdateServerSelect() le consomme (edge, une
    // seule fois) au tick suivant.
    bool transitionRequested = false;
};

// --- Point d'intégration réseau (callbacks optionnels, nullptr = comportement neutre) ---
// Le binaire construit la table de serveurs directement en mémoire (pas d'E/S) ; seule
// l'interrogation de population est asynchrone (CreateThread). On expose donc UNIQUEMENT
// ce point d'intégration, en gardant BuildServerList() pure/synchrone.

// Résultat de Net_QueryServerStatus 0x519CC0 (TCP connect + recv d'un record de statut
// de 17 octets). Le binaire dépose 3 dwords : maxPopulation (octets 5-8 -> this[12371+i]),
// loadStep (octets 9-12 -> this[13371+i]) et retourne currentPopulation (octets 13-16 ->
// this[14371+i]). Sur échec (socket/gethostbyname/connect/recv incomplet) le binaire
// retourne -1 SANS écrire maxPopulation/loadStep (elles gardent leur valeur précédente) —
// modélisé ici par currentPopulation < 0 = échec/en cours.
struct ServerStatus {
    int32_t maxPopulation     = 0;   // record[5..8]  : capacité max du serveur
    int32_t loadStep          = 0;   // record[9..12] : pas de population par palier de barre
    int32_t currentPopulation = -1;  // record[13..16] (valeur de retour) : -1 = échec/en cours
};

struct ServerSelectHost {
    // Net_QueryServerStatus 0x519CC0 : interroge un serveur (nom, port) et renvoie son
    // record de statut. currentPopulation < 0 = interrogation encore en cours / échec
    // (fidèle à -1 ; maxPopulation/loadStep alors ignorés côté appelant).
    std::function<ServerStatus(const std::string& name, uint16_t port)> QueryServerStatus;

    // Cfg_SaveLastServer 0x519C40 : persiste le dernier groupe/serveur choisi (config
    // utilisateur, G02_GINFO\010.BIN). Implémentation réelle byte-exacte disponible en
    // Config/GameOptions.h::ts2::config::Cfg_SaveLastServer(int32_t) — ce module de
    // référence n'étant pas câblé dans le flux compilé (cf. en-tête de fichier), le
    // câblage concret vit dans UI/LoginScene.cpp::ServerSelectOnMouseDown ; un futur
    // consommateur de ServerSelectFlow.cpp n'a qu'à brancher ce callback dessus.
    std::function<void(int32_t groupIndex, int32_t serverIndex)> SaveLastServer;
};

// Construit/reconstruit la table de serveurs (Scene_ServerSelectUpdate, sous-état 0,
// à 30 frames). Fidèle : mode SingleServer -> un seul serveur "12sky2-login.geniusorc.com"
// port 8088 (this[15372]=this[15373]=0) ; mode MultiChannel -> 6 serveurs, ports EXACTS et
// DANS L'ORDRE du binaire 10005/10205/10305/11096/11095/11092 (11096 AVANT 11095, EA
// 0x518fac-0x519164, this[15373]=2). Fixe aussi backgroundImageId=2380, selectedGroup=0,
// selectedGroupBtnLo=0. `shuffleGroups` est un point d'intégration optionnel pour reproduire
// le mélange d'ordre des groupes du binaire (this[170..], avec 1 groupe l'ordre est trivial).
void BuildServerList(ServerSelectState& state, ServerListMode mode,
                      const std::function<void(std::vector<int32_t>&)>& shuffleGroups = nullptr);

// Interroge (de façon SYNCHRONE ici) le statut des serveurs de la PLAGE de boutons active
// [selectedGroupBtnLo..selectedGroupBtnHi] (= dword_16851B0..B4, exactement ce que boucle
// Net_ServerStatusThread 0x518AB0), via host.QueryServerStatus. Applique maxPopulation/
// loadStep/currentPopulation de chaque serveur depuis le record renvoyé (sur échec,
// currentPopulation=-1, maxPopulation/loadStep inchangés — fidèle). Reproduit le rôle du
// thread sans le vrai threading : à l'appelant de la piloter périodiquement (ou de la
// lancer sur un thread) pour reproduire l'asynchronisme exact.
void PollServerStatuses(ServerSelectState& state, const ServerSelectHost& host);

// Update par frame (30 FPS fixe — dt accepté pour cohérence d'API avec le reste du
// projet, mais la logique d'origine compte des FRAMES entières, pas des secondes : un
// appel = un pas fixe, comme App_FrameTick -> cSceneMgr_Update). Fait avancer le
// sous-état Init->Idle après 30 frames (construit la liste au passage si nécessaire) et
// consomme le flag armé par OnServerClicked(). Retourne true UNE SEULE FOIS, le tick où
// la transition vers Login (scene_id=3) est prête à être appliquée par l'appelant.
bool UpdateServerSelect(ServerSelectState& state, float dt);

// Clic sur un en-tête de groupe/canal (avant sélection d'un serveur précis). Fidèle
// (Scene_ServerSelectOnMouseDown 0x519780, branche "clic groupe", EA 0x5197eb-0x51995b) :
// no-op si le groupe est déjà sélectionné (garde this[15371] != group) ; sinon met
// selectedGroup=group, selectedGroupBtnLo=5*group, selectedGroupBtnHi=5*group+4 (pour
// groupes 0 et 1 ; au-delà btnHi inchangé), persiste (host.SaveLastServer), remet à -1 le
// statut de la plage puis relance l'interrogation (PollServerStatuses).
void OnGroupClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t groupIndex);

// Clic sur un serveur précis. Fidèle (EA 0x5199a2-0x519a3f) : cliquable seulement si
// population connue (currentPopulation >= 0) ET strictement currentPopulation < maxPopulation
// (this[12371], alimentée par le serveur). ATTENTION : PAS d'échappatoire "maxPopulation<=0"
// — tant que le serveur n'a pas renvoyé sa capacité (maxPopulation==0), aucun clic n'est
// validé, EXACTEMENT comme le binaire. Si valide : enregistre la sélection (selectedServer),
// persiste le choix (host.SaveLastServer) et arme la transition ; retourne true (transition
// prête, consommable immédiatement OU au prochain UpdateServerSelect()).
bool OnServerClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t serverIndex);

// Relâchement de clic sur le bouton d'action/retour (Scene_ServerSelectOnMouseUp
// 0x519AC0) : dans le binaire ouvre une confirmation modale de retour arrière — PAS de
// logique de flux d'état associée (TODO rendu/UI : ouvrir la NoticeDlg de confirmation).
// Exposée pour complétude de l'API d'entrée souris de la scène.
inline void OnActionButtonReleased(ServerSelectState& /*state*/) {
    // TODO UI : NoticeDlg de confirmation "retour arrière" (UI_NoticeDlg_Open 0x5C0280).
}

} // namespace ts2::game

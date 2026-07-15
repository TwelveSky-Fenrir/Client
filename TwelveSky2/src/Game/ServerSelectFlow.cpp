// Game/ServerSelectFlow.cpp — implémentation de la machine de flux ServerSelect.
// Voir ServerSelectFlow.h pour les EA d'origine et le détail du flux découvert.
#include "Game/ServerSelectFlow.h"
#include <algorithm> // std::max (clamp de la plage de boutons interrogée)

namespace ts2::game {

namespace {

// Attente initiale du sous-état 0 avant (re)construction de la liste, EXACTEMENT
// 30 frames (0x1E) dans le binaire, comme les autres temporisations de transition de
// scène (cf. Docs/TS2_CLIENT_SHELL.md : "compteur de frames ... 0x1E=1s").
constexpr int32_t kInitWaitFrames = 30;

// Ports EXACTS et DANS L'ORDRE relevés dans Scene_ServerSelectUpdate 0x518B30 pour la
// liste multi-canaux (branche prise quand g_ServerModeFlag/dword_166918C n'est ni 0, ni 1,
// ni 2). ATTENTION à l'ordre : 11096 (this[10374], EA 0x5190b4) précède 11095 (this[10375],
// EA 0x51910c) ; le 6e serveur (11092) est écrit à l'indice SPARSE 40 (this[10411], EA
// 0x519164), fusionné ici en 6e entrée dense.
constexpr uint16_t kMultiChannelPorts[] = {10005, 10205, 10305, 11096, 11095, 11092};
constexpr int32_t  kMultiChannelCount   = static_cast<int32_t>(sizeof(kMultiChannelPorts) / sizeof(kMultiChannelPorts[0]));

// Port EXACT de la liste mono-serveur (branche g_ServerModeFlag == 0, 1 ou 2). EA 0x518d91.
constexpr uint16_t kSingleServerPort = 8088;

// Hôte login CODÉ EN DUR poussé par le binaire vers this[371] (string 0x7A96F8, EA
// 0x518d77) pour g_ServerModeFlag==0 = net::kLoginHostCom. Reproduit tel quel (le binaire
// hardcode cette chaîne, pas de fichier de config — cf. Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md §2).
constexpr const char* kSingleServerHost = "12sky2-login.geniusorc.com";

// Id de sprite de fond (this[168]) : le binaire tire 2380 ou 2381 (Rng_Next()%2, EA
// 0x518c29-0x518c40). 2380 par défaut ici (valeur RÉELLE, pas inventée) faute de RNG injecté.
constexpr int32_t kDefaultBackgroundImageId = 2380;

} // namespace

void BuildServerList(ServerSelectState& state, ServerListMode mode,
                      const std::function<void(std::vector<int32_t>&)>& shuffleGroups) {
    state.servers.clear();
    state.groupOrder.clear();

    // this[169] = nombre de GROUPES : le binaire le fixe à 1 dans TOUTES les branches
    // (EA 0x518cff SingleServer, 0x518e0b mode 1/2, 0x518f6e MultiChannel). Les "canaux"
    // MultiChannel sont des SERVEURS du groupe unique, pas des groupes distincts.
    state.groupCount = 1;

    if (mode == ServerListMode::SingleServer) {
        ServerEntry e;
        e.name              = kSingleServerHost; // this[371] = "12sky2-login.geniusorc.com" (EA 0x518d77)
        e.port              = kSingleServerPort; // this[10371] = 0x1F98 = 8088 (EA 0x518d91)
        e.maxPopulation     = 0;                 // this[12371] = 0 (EA 0x518dab) — alimenté ensuite par le serveur
        e.loadStep          = 0;                 // this[13371] = 0 (EA 0x518db8)
        e.currentPopulation = -1;                // this[14371] = -1 (EA 0x518dc5)
        e.groupIndex        = 0;
        state.servers.push_back(e);
        state.selectedGroupBtnHi = 0;            // this[15373] = 0 (EA 0x518dec) : 1 seul bouton visible
    } else {
        for (int32_t i = 0; i < kMultiChannelCount; ++i) {
            ServerEntry e;
            e.name              = "Channel " + std::to_string(i + 1); // libellé non stocké par le binaire (boutons graphiques)
            e.port              = kMultiChannelPorts[i];
            e.maxPopulation     = 0;
            e.loadStep          = 0;
            e.currentPopulation = -1;
            e.groupIndex        = 0;             // tous dans le groupe unique 0
            state.servers.push_back(e);
        }
        state.selectedGroupBtnHi = 2;            // this[15373] = 2 (EA 0x5191bf) : boutons 0..2 visibles
    }

    // Ordre d'affichage des groupes (this[170..170+groupCount-1]) : identité par défaut,
    // mélangé si un shuffler est fourni. Avec 1 groupe, c'est trivialement [0].
    state.groupOrder.resize(state.groupCount);
    for (int32_t i = 0; i < state.groupCount; ++i) {
        state.groupOrder[i] = i;
    }
    if (shuffleGroups) {
        shuffleGroups(state.groupOrder);
    }

    state.backgroundImageId  = kDefaultBackgroundImageId; // this[168] = 2380/2381 (Rng_Next()%2, EA 0x518c29)
    state.selectedGroup      = 0;   // this[15371] = 0 (EA 0x518dd2 / 0x5191a5)
    state.selectedGroupBtnLo = 0;   // this[15372] = 0 (EA 0x518ddf / 0x5191b2)
    state.selectedServer     = -1;  // this[15374] = -1 (EA 0x5191cc)
    state.listBuilt          = true;
}

void PollServerStatuses(ServerSelectState& state, const ServerSelectHost& host) {
    if (!host.QueryServerStatus) {
        return;
    }
    // Net_ServerStatusThread 0x518AB0 : `for (i = dword_16851B0; i <= dword_16851B4; ++i)`
    // — ces deux globales SONT les mots this[15372]/this[15373] (= selectedGroupBtnLo/Hi).
    // On interroge donc EXACTEMENT cette plage de boutons.
    const int32_t lo = state.selectedGroupBtnLo;
    const int32_t hi = state.selectedGroupBtnHi;
    const int32_t last = static_cast<int32_t>(state.servers.size()) - 1;
    for (int32_t i = std::max(0, lo); i <= hi && i <= last; ++i) {
        ServerEntry& s = state.servers[static_cast<size_t>(i)];
        const ServerStatus st = host.QueryServerStatus(s.name, s.port);
        // Fidèle à Net_QueryServerStatus : sur succès (record de 17 o reçu) le binaire
        // écrit maxPopulation (a3), loadStep (a4) ET currentPopulation (retour) ; sur
        // échec il retourne -1 SANS toucher a3/a4 (maxPopulation/loadStep conservés).
        if (st.currentPopulation >= 0) {
            s.maxPopulation     = st.maxPopulation;
            s.loadStep          = st.loadStep;
            s.currentPopulation = st.currentPopulation;
        } else {
            s.currentPopulation = -1;
        }
    }
    state.statusThreadActive = true;
}

bool UpdateServerSelect(ServerSelectState& state, float /*dt*/) {
    switch (state.subState) {
    case ServerSelectSubState::Init: {
        ++state.frameCounter;
        if (state.frameCounter < kInitWaitFrames) {
            return false;
        }
        state.frameCounter = 0;

        // TODO rendu : UI_ResetAllDialogs 0x5AC3F0 (remet l'UI à plat à l'entrée de la scène).
        // TODO audio : Snd_LoadOggToBuffers("Z000.BGM", mode 3) + Snd_Play3D si g_BgmEnabled==1.
        if (!state.listBuilt) {
            // TODO fidélité : mode réel piloté par g_ServerListMode (dword_166918C) côté
            // appelant ; SingleServer utilisé par défaut faute de branchement externe.
            BuildServerList(state, ServerListMode::SingleServer);
        }

        // CreateThread(Net_ServerStatusThread 0x518AB0) dans le binaire : ici on se
        // contente d'armer le drapeau — à l'appelant de piloter PollServerStatuses()
        // périodiquement (ou sur un vrai thread) pour reproduire l'asynchronisme exact.
        state.statusThreadActive = true;

        state.subState = ServerSelectSubState::Idle;
        return false;
    }
    case ServerSelectSubState::Idle: {
        // Idle : attend un clic (OnGroupClicked / OnServerClicked). Le heartbeat
        // anti-triche (sub_6DE3F7, toutes les 30 frames dans le binaire) est
        // volontairement IGNORÉ ici — hors périmètre de cette mission (cf. CLAUDE.md).
        if (state.transitionRequested) {
            state.transitionRequested = false;
            return true;
        }
        return false;
    }
    }
    return false;
}

void OnGroupClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t groupIndex) {
    if (groupIndex < 0 || groupIndex >= state.groupCount) {
        return; // hors table : aucun effet, fidèle au binaire (hit-test raté)
    }
    // Fidèle (EA 0x51987e) : garde `this[15371] != group` — cliquer le groupe déjà
    // sélectionné est un no-op (aucun son, aucune relance de statut). NB : le binaire
    // résout le groupe via l'ordre d'affichage this[i+170] (groupOrder) ; ici groupIndex
    // est déjà l'id de groupe résolu.
    if (groupIndex == state.selectedGroup) {
        return;
    }

    state.selectedGroup = groupIndex;
    // btnLo = 5*group ; btnHi = 5*group+4 pour groupes 0 et 1 (5 serveurs-boutons/groupe,
    // EA 0x5198ba-0x519904). Au-delà (group>=2) le binaire NE met PAS à jour btnHi.
    state.selectedGroupBtnLo = 5 * groupIndex;
    if (groupIndex == 0 || groupIndex == 1) {
        state.selectedGroupBtnHi = 5 * groupIndex + 4;
    }

    // Cfg_SaveLastServer AVANT la relance du statut (ordre du binaire, EA 0x51990d).
    if (host.SaveLastServer) {
        host.SaveLastServer(groupIndex, state.selectedServer);
    }

    // Remet à -1 le statut de toute la plage de boutons du groupe (EA 0x51991b-0x51993d),
    // puis relance l'interrogation (CreateThread(Net_ServerStatusThread) EA 0x51995b —
    // PollServerStatuses boucle la MÊME plage [btnLo..btnHi]).
    const int32_t last = static_cast<int32_t>(state.servers.size()) - 1;
    for (int32_t i = std::max(0, state.selectedGroupBtnLo); i <= state.selectedGroupBtnHi && i <= last; ++i) {
        state.servers[static_cast<size_t>(i)].currentPopulation = -1;
    }
    PollServerStatuses(state, host);
}

bool OnServerClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t serverIndex) {
    if (serverIndex < 0 || serverIndex >= static_cast<int32_t>(state.servers.size())) {
        return false; // hors table : aucun effet
    }

    const ServerEntry& s = state.servers[serverIndex];

    // Fidèle : population encore inconnue (interrogation en cours) -> pas de sélection.
    if (s.currentPopulation < 0) {
        return false;
    }
    // Fidèle (EA 0x519a19) : sélection UNIQUEMENT si currentPopulation < maxPopulation
    // (this[12371], alimentée par le serveur). PAS d'échappatoire "maxPopulation<=0" :
    // tant que la capacité n'a pas été reçue (maxPopulation==0), le clic est refusé,
    // EXACTEMENT comme le binaire (0 < 0 est faux, donc pas de sélection).
    if (s.currentPopulation >= s.maxPopulation) {
        return false;
    }

    state.selectedServer = serverIndex;
    state.transitionRequested = true; // consommé par UpdateServerSelect() -> true

    if (host.SaveLastServer) {
        host.SaveLastServer(state.selectedGroup, serverIndex);
    }

    return true; // transition vers Login (scene_id=3) prête
}

} // namespace ts2::game

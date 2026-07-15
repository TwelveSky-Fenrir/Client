// Game/EnterWorldFlow.h — Machine d'état d'ENTRÉE EN MONDE (scène EnterWorld = 5).
//
// Réécriture C++ fidèle de Scene_EnterWorldUpdate 0x52BFF0 (dispatché par
// cSceneMgr_Update 0x517BF0 quand g_SceneMgr.sceneId == Scene::EnterWorld). Source
// UNIQUE de vérité : décompilation Hex-Rays via idaTs2 (voir rapport de session).
//
// FLUX RÉEL DÉCOUVERT (4 sous-états explicites + 1 état d'échec terminal) :
//
//   0 WaitBeforeUnload  : attend 30 frames (0x1E) puis purge l'UI/audio résiduels de
//                         CharSelect, remet à zéro l'index de préchargement de zone,
//                         capture zoneId-1 comme "zone précédente" -> état 1.
//   1 LoadZoneResources : toutes les 10 frames (0xA), appelle
//                         World_LoadZoneResource(dword_14A883C, dword_1675A9C, idx) avec idx
//                         = 0..19 (compteur brut, PAS une table de lookup) ; après idx==20e
//                         incrément (20 appels) -> état 2. => 200 frames (~6,67 s à 30 FPS).
//                         AUDIT 2026-07-14 (décompilation directe de World_LoadZoneResource
//                         0x4DCB60, cf. World/WorldMap.h::ResourceKind) : `idx` EST
//                         directement le paramètre a3 du dispatch, SANS indirection. Le
//                         switch de 0x4DCB60 ne couvre QUE les valeurs 1..12 (= les 12
//                         ResourceKind existants, WorldMap.h::ResourceKind est donc DÉJÀ
//                         COMPLET, pas un sous-ensemble). idx==0 et idx==13..19 (8 valeurs
//                         sur 20) tombent dans le `default` du switch d'origine, qui ne fait
//                         RIEN (renvoie a3 tronqué en char, aucun effet de bord) — ce ne
//                         sont PAS 20 "types de ressource" distincts mais 12 chargements
//                         réels + 8 itérations no-op qui ne servent qu'à consommer du temps
//                         (fidélité du timing 200 frames, pas du chargement). Câblage
//                         correct côté host.LoadZoneResource : caster idx en
//                         world::ResourceKind et appeler WorldMap::LoadZoneResource(zoneId,
//                         kind) pour idx∈[1,12] ; idx==0/[13,19] peuvent être ignorés sans
//                         rien casser (le binaire ne fait rien non plus pour ces valeurs).
//   2 SendEnterRequest  : attend 30 frames (0x1E) puis envoie la requête d'entrée en
//                         monde (Net_SendPacket_Op12, opcode 12, 222 o). Succès -> état 3
//                         (WaitServerAck). Échec d'émission -> notice d'erreur (StrTable005
//                         id 67) -> état 4 (Failed).
//   3 WaitServerAck     : attente PASSIVE avec timeout 5000 frames (0x1388, ~166 s). Le
//                         client N'INITIE PAS lui-même la bascule vers InGame : c'est le
//                         paquet serveur Pkt_EnterWorld (opcode 12 entrant, EA 0x464160,
//                         RÉSEAU — hors périmètre de ce module) qui, en le recevant, écrit
//                         directement g_SceneMgr.sceneId=InGame et subState=0. Si l'ACK
//                         n'arrive jamais avant le timeout : notice (StrTable005 id 68) ->
//                         état 4 (Failed). update() ne peut donc QUE détecter le timeout ;
//                         la sortie normale de cet état est observée par l'appelant via le
//                         changement de scène (SceneManager), pas via la valeur de retour.
//   4 Failed            : terminal, aucune progression (comportement fidèle : le "default"
//                         du switch d'origine ne fait rien et retourne).
//
// Écart connu / TODO : le code d'origine fait aussi, au passage état2, une manipulation de
// g_SelfMorphNpcId (sauvegarde dans dword_1675A94 puis écrasement par la zone cible,
// dword_1675A9C) et, à la réception de Pkt_EnterWorld, remet à zéro plusieurs compteurs
// (dword_16760D8/DC/E0), recalcule un palier de croissance (dword_1675D90 = f(g_GrowthIndex))
// et peut ré-émettre des commandes GM en attente (téléport/vault, dword_1675A8C == 5/8/9).
// Cette partie appartient au système de morph/téléport (SkillCombat/MapWarp) et à
// Pkt_EnterWorld lui-même (paquet réseau) : DÉLIBÉRÉMENT PAS reproduite ici (hors
// périmètre "flux de scène pur"), simplement documentée pour le câblage ultérieur.
//
// GameGuard/anticheat (Ac_*) : Scene_EnterWorldUpdate n'en contient PAS directement (le
// polling GameGuard est fait par les AUTRES scènes en ligne, cf. Scene_InGameUpdate côté
// InGameTickFlow) — rien à ignorer ici, confirmé par lecture du désassemblage.
//
// Rendu 3D : aucun rendu dans cette fonction d'origine (elle est pure logique/état) — pas
// de TODO de rendu à noter pour ce module.
//
// Autonomie : ce module n'inclut que la STL. Les actions à effet de bord (I/O UI, réseau,
// chargement d'assets) sont exposées via EnterWorldFlowHost (callbacks), à brancher par
// l'appelant — AUCUN couplage à Scene/SceneManager.h ni à un singleton global.
#pragma once
#include <functional>

namespace ts2::game {

enum class EnterWorldState : int {
    WaitBeforeUnload  = 0, // case 0 @0x52BFF9
    LoadZoneResources = 1, // case 1 @0x52C0CF
    SendEnterRequest  = 2, // case 2 @0x52C149
    WaitServerAck     = 3, // case 3 @0x52C1F3
    Failed            = 4, // default @0x52C232 (état terminal, plus de code de scène dédié)
};

// État persistant de la machine (équivalent de g_SceneMgr.subState/frameCounter pendant
// que sceneId == EnterWorld, + les 2 scratch propres à cette scène : index de ressource de
// zone (this+15726 d'origine) et zone précédente capturée (this+15727)).
struct EnterWorldFlowState {
    EnterWorldState state = EnterWorldState::WaitBeforeUnload;
    int frameCounter       = 0;  // g_SceneMgr.frameCounter (dword_1676188 d'origine)
    int zoneResourceIndex  = 0;  // index 0..19 de préchargement (this+15726)
    int previousZoneId     = -1; // zoneId - 1 capturé à l'entrée dans LoadZoneResources (this+15727)
};

// Points d'intégration (effets de bord hors périmètre de ce module — réseau/UI/assets).
struct EnterWorldFlowHost {
    // UI_ResetAllDialogs(&unk_1821D4C) 0x5AC3F0 + sub_4C1110(0) 0x4C1110 (reset tooltip) +
    // UI_FocusEditBox(&g_UIEditBoxMgr,0) 0x50F4A0 + Snd_ReleaseBuffers 0x6A80D0 : purge
    // regroupée de l'UI et de l'audio résiduels de CharSelect. Un seul hook.
    std::function<void()> ResetUiAndAudio;

    // World_LoadZoneResource(dword_14A883C, zoneId, resourceIndex) 0x4DCB60 : `resourceIndex`
    // (0..19) EST DIRECTEMENT le paramètre a3 du dispatch d'origine (pas d'indirection) —
    // voir l'audit en tête de fichier. Seules les valeurs 1..12 correspondent à un
    // world::ResourceKind réel (World/WorldMap.h) et déclenchent un chargement ; 0 et
    // 13..19 sont des no-op fidèles (le binaire d'origine ne fait rien non plus pour ces
    // valeurs — switch `default`). L'appelant (SceneManager) doit caster resourceIndex en
    // world::ResourceKind et appeler WorldMap::LoadZoneResource(zoneId, kind) directement ;
    // ce module reste volontairement découplé de World/WorldMap.h (leaf STL-only, cf. note
    // "Autonomie" en tête de fichier). Bloquant côté original.
    std::function<void(int zoneId, int resourceIndex)> LoadZoneResource;

    // Net_SendPacket_Op12(client, g_AccountName, ..., ...) 0x4B43C0 : émet la requête
    // d'entrée en monde (222 o : nom 128o + 13o + 72o). Retour = succès d'émission, TESTÉ
    // par le code d'origine (if(result)) pour choisir WaitServerAck vs Failed.
    std::function<bool()> SendEnterWorldRequest;

    // UI_NoticeDlg_Open(2, StrTable005_Get(g_LangId, strId), "") 0x5C0280 : notice modale
    // d'erreur. strId = 67 (échec d'émission Op12) ou 68 (timeout ACK serveur).
    std::function<void(int strId)> ShowErrorNotice;
};

// Scene_EnterWorldUpdate 0x52BFF0. Appeler 1x/frame (30 FPS) tant que la scène active est
// EnterWorld. `zoneId` = identifiant de la zone cible à charger (dword_1675A9C d'origine,
// déjà résolu en amont par CharSelect/Pkt_GameServerConnectResult — fourni par l'appelant,
// PAS lu depuis un global ici).
//
// Retour : false une fois l'état Failed atteint (plus aucune progression possible sans
// action UI hors périmètre) ; true sinon — y COMPRIS pendant WaitServerAck, où l'appelant
// doit détecter la sortie réelle (transition vers InGame) via un canal externe (dispatch
// réseau -> changement de scène), pas via cette valeur de retour.
bool EnterWorldFlow_Update(EnterWorldFlowState& state, const EnterWorldFlowHost& host, int zoneId);

} // namespace ts2::game

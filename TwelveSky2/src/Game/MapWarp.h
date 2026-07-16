// Game/MapWarp.h — Système de WARP DE FACTION (retour ville / téléportation d'appoint).
// Réécriture C++ PROPRE (byte-exact sur seuils/offsets/formules) de :
//   Map_BeginWarpToFactionTown     0x55C510 (__thiscall(this=g_LocalPlayerSheet, a2=mode))
//   Map_BeginWarpToFactionTownDefault 0x55C740 (aucun this/mode — variante "toujours forcée")
//   Map_BeginWarpToMap37           0x55C8A0 (__thiscall(this=g_LocalPlayerSheet) — carte fixe 37)
//   Map_BeginWarpToFactionTownEx   0x55C9A0 (__thiscall(this=g_LocalPlayerSheet, a2=mode))
//
// RÉFÉRENCÉ PAR (TODOs à résoudre par ce module) :
//   Net/GameHandlers_BossWorld.cpp  — 0x60 ZoneBuffStatus  (TODO Map_BeginWarpToFactionTown(0))
//   Net/GameHandlers_InvDispatch.cpp / GameHandlers_Misc.cpp — 0x58 CultivationDispatch,
//     0x16 SetGameVar (longue traîne de cas "value<=0 -> warp")
//   Net/GameVarDispatch.cpp — stubs locaux Map_BeginWarpToFactionTown[Ex] (0x468370)
//   Game/AutoPlaySystem.h   — host.WarpToFactionTown (Map_BeginWarpToFactionTownEx(0))
//
// PÉRIMÈTRE (imposé par la mission) : ce module calcule la RÉSOLUTION (ville/NPC cible,
// coordonnées si un résolveur est fourni, code de warp) et les GARDES (mort/cooldown/
// morph déjà en cours), et pose l'INTENTION de warp dans les globals "longue traîne"
// (g_Client.Var, mêmes adresses que le binaire). Quand un NetClient est fourni, il émet
// désormais le paquet de warp réel (Net_SendWarpRequest = Op20 0x4B5000) ET l'Op99 auto-hunt
// (Net_SendAutoHuntSync = 0x4BD140, blobs à zéro tant que la config auto-hunt n'est pas
// modélisée — cf. MapWarp.cpp EmitAutoHuntSync). RESTENT HORS PÉRIMÈTRE (TODO précis à chaque
// point d'usage, EA citée) : le rendu monde (World_LoadMap, déjà écrit dans World/WorldMap.*)
// et les émetteurs op0/op1 Net_QueueMoveTo/RespawnMove (module PlayerCmdController non implémenté).
//
// ---------------------------------------------------------------------------------------
// Dérivation de "*(this+1784)" (this=g_LocalPlayerSheet 0x1685748, index DWORD) :
//   0x1685748 + 1784*4 = 0x1685748 + 0x1BE0 = 0x1687328.
//   0x1687328 - 0x1687234 (dword_1687234, tableau joueurs, stride 908/0x38C) = 0xF4 = 244.
//   => C'est EXACTEMENT players[0]+244, le champ d'état de la FSM d'action documenté dans
//   Game/ActionStateMachine.h (CharActionState, entity+244 — non inclus ici pour rester
//   autonome ; la valeur numérique 12 = CharActionState::DeathRespawn y est dupliquée en
//   constante locale kDeathRespawnState). g_World.players[0] == self (cf. GameState.h).
//   Le champ est lu dans PlayerEntity::body à l'offset 220 (244-24, le corps commençant à
//   +0x18) — même convention que Game/EntityManager.cpp (kPActionState=220).
// ---------------------------------------------------------------------------------------
#pragma once
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/EntityManager.h"
#include <cstdint>

// Forward-decl seule (evite de tirer winsock/Net dans les ~8 consommateurs de
// MapWarp.h) : l'include complet Net/SendPackets.h n'est fait que dans MapWarp.cpp,
// meme schema eprouve que Game/ComboPickupTick.
namespace ts2::net { struct NetClient; }

namespace ts2::game {

// ---------------------------------------------------------------------------
// Adresses d'origine des globals "longue traîne" utilisés par le warp (clés stables
// pour g_Client.Var/VarF — mêmes adresses que Net/GameVarDispatch.cpp et
// Net/ClientState.h, non incluses ici pour rester autonome vis-à-vis du réseau).
// ---------------------------------------------------------------------------
namespace WarpAddr {
    constexpr uint32_t SelfMorphNpcId  = 0x1675A98; // g_SelfMorphNpcId — id NPC/ville "courant" du joueur
    constexpr uint32_t MorphInProgress = 0x1675A88; // g_MorphInProgress — 1 = un morph/warp est déjà armé
    constexpr uint32_t CooldownLatch   = 0x1675B08; // g_GmCmdCooldownLatch — verrou anti-renvoi (Ex uniquement)
    constexpr uint32_t InvDirtyEnable  = 0x16755AC; // g_InvDirtyEnable — désactivé pendant un déplacement/warp

    // Bloc "warp en cours d'armement" (dword_1675A8C..flt_1675AC8), remis à zéro par
    // Crt_Memset(&dword_1675AA0, 0, 72) puis réécrit champ par champ dans le binaire.
    constexpr uint32_t WarpModeCode    = 0x1675A8C; // dword_1675A8C — code envoyé au serveur (3, 7 ou 11)
    constexpr uint32_t WarpSub         = 0x1675A90; // dword_1675A90 — toujours 0 dans les 4 fonctions
    constexpr uint32_t WarpTargetNpc   = 0x1675A9C; // dword_1675A9C — id NPC/ville cible (== townNpcId) ;
                                                    //   MEME global que g_TargetZoneId du binaire (Warp_SendTeleport 0x5F5CE0, EA 0x5f5d46)

    constexpr uint32_t WarpFlagA0      = 0x1675AA0; // dword_1675AA0 — toujours 0
    constexpr uint32_t WarpFlagA4      = 0x1675AA4; // dword_1675AA4 — 0 (mode forcé) ou 1 (mode normal)
    constexpr uint32_t WarpDelay       = 0x1675AA8; // flt_1675AA8 — toujours 0.0
    constexpr uint32_t WarpPosX        = 0x1675AAC; // flt_1675AAC
    constexpr uint32_t WarpPosY        = 0x1675AB0; // flt_1675AB0
    constexpr uint32_t WarpPosZ        = 0x1675AB4; // flt_1675AB4
    constexpr uint32_t WarpFacingA     = 0x1675AC4; // flt_1675AC4 — angle aléatoire 0..359
    constexpr uint32_t WarpFacingB     = 0x1675AC8; // flt_1675AC8 — miroir de WarpFacingA
} // namespace WarpAddr

// Valeur numérique de CharActionState::DeathRespawn (Game/ActionStateMachine.h, 0x0C) —
// dupliquée ici (constante, pas d'include croisé) pour tester l'état "mort" du joueur.
inline constexpr int32_t kDeathRespawnState = 12;

// Offset du champ d'état d'action dans PlayerEntity::body (== Game/EntityManager.cpp
// kPActionState). players[0] == self (GameState.h).
inline constexpr std::size_t kSelfActionStateOffset = 220;

// ---------------------------------------------------------------------------
// Résolution de coordonnées de ville — HORS PÉRIMÈTRE (données d'assets/tables NPC) :
//   Motion_GetComboOffsetTable 0x5025E0 (élément, npcId) -> vec3, table de "combo" liée
//   aux animations de morph, échoue (retourne 0) hors de certains contextes ;
//   GInfo2_GetVec3 0x4FD4C0 (flt_1555D08, npcId) -> vec3, table globale d'info NPC
//   (fallback systématique dans les 3 fonctions faction). flt_1555D08 = base d'une table
//   de records NPC chargée depuis les .IMG (hors périmètre de ce module).
// La couche appelante (qui a accès aux tables .IMG/NPC déjà chargées) peut brancher ce
// résolveur ; sans lui, `x`/`y`/`z` restent à 0 dans la résolution (townNpcId reste
// correct et exploitable par le rendu monde pour retrouver la position par ailleurs).
// ---------------------------------------------------------------------------
class IFactionTownCoordResolver {
public:
    virtual ~IFactionTownCoordResolver() = default;
    // Fidèle à l'appel d'origine : d'abord Motion_GetComboOffsetTable(element, townNpcId),
    // puis si échec GInfo2_GetVec3(npcId) — l'implémentation reproduit l'un ou l'autre (ou
    // les deux) selon ce qui est disponible côté données. Renvoie false si aucune position
    // n'a pu être résolue (x/y/z laissés à 0 par l'appelant).
    virtual bool ResolveTownCoords(int32_t element, int32_t townNpcId, float& x, float& y, float& z) const = 0;
};

// Action concrète que le binaire aurait déclenchée pour cette résolution (le réseau/monde
// réel restent hors périmètre — cf. TODOs dans MapWarp.cpp).
enum class WarpAction : uint8_t {
    None,        // rien à faire (faction inconnue, déjà sur place [Town/Default], bloqué)
    ArmFullWarp, // arme le warp complet + Net_SendPacket_Op20 (téléportation de carte)
    MoveInPlace, // déjà sur la bonne ville (Ex uniquement) -> Net_QueueMoveTo/RespawnMove + Op99
};

// Résultat de résolution d'une demande de warp de faction — AUCUN effet de rendu/réseau,
// uniquement la décision + les paramètres qu'un appelant (couche réseau/monde) doit
// ensuite exécuter (TODO précis cités en commentaire de chaque champ concerné).
struct FactionWarpResolution {
    bool valid = false;   // false si `element` ne correspond à aucune ville (switch default)
    WarpAction action = WarpAction::None;

    // Gardes qui ont bloqué la demande (mutuellement exclusifs dans la trace d'origine,
    // exposés séparément pour le diagnostic/log appelant).
    bool blockedByDeath           = false; // *(g_LocalPlayerSheet+1784) lié à l'état mort/vivant (cf. dérivation en tête de fichier)
    bool blockedByMorphInProgress = false; // g_MorphInProgress déjà à 1
    bool blockedByCooldown        = false; // g_GmCmdCooldownLatch (Ex uniquement, branche "pas déjà sur place")

    int32_t element    = 0;  // g_LocalElement passé en entrée
    int32_t townNpcId  = 0;  // id NPC/ville cible (table ci-dessous) ; sert aussi d'identifiant
                              // de "zone" côté serveur pour le paquet de warp (Net_SendPacket_Op20).
    bool     coordsResolved = false; // true si un IFactionTownCoordResolver a fourni une position
    float x = 0.0f, y = 0.0f, z = 0.0f; // position cible (0 si non résolue)

    int32_t warpModeCode = 0; // dword_1675A8C armé (3=Town forcé, 7=Town/Default/Ex normal, 11=Map37)
    float   facingDeg    = 0.0f; // flt_1675AC4/AC8 armés (angle aléatoire 0..359)
};

// Table faction -> ville EXACTE (switch(g_LocalElement) identique dans les 3 fonctions
// faction 0x55C510/0x55C740/0x55C9A0) : element 0..3 -> id NPC de ville. Toute autre
// valeur (élément inconnu/observateur transitoire) -> 0 (aucune ville, warp abandonné).
// NB : element==3 (mode observateur, cf. Net_OnToggleObserver 0x28) résout bien sur 140 —
// cohérent avec le fait que l'observateur a sa propre "ville" logique.
inline int32_t FactionTownNpcId(int32_t element) {
    switch (element) {
    case 0: return 1;
    case 1: return 6;
    case 2: return 11;
    case 3: return 140;
    default: return 0;
    }
}

// ---------------------------------------------------------------------------
// API principale.
// ---------------------------------------------------------------------------

// Map_BeginWarpToFactionTown 0x55C510 (ex=false) / Map_BeginWarpToFactionTownEx 0x55C9A0
// (ex=true). `mode` == a2 d'origine (toujours 0 sur tous les sites d'appel observés SAUF
// Char_TickDeathRespawn qui appelle la variante Ex avec mode=1 lors de la mort du joueur).
// `resolver` peut être nullptr (x/y/z resteront à 0, cf. IFactionTownCoordResolver).
// `nc` (trailing, défaut nullptr) : si fourni, l'émission réseau réelle (Net_SendWarpRequest
// = Op20 0x4B5000) est effectuée ; sinon comportement "résolution seule" préservé (tous les
// appelants actuels passent nullptr). Le câblage effectif du NetClient relève de fronts ultérieurs.
FactionWarpResolution BeginWarpToFactionTown(int32_t element, bool ex = false, int32_t mode = 0,
                                              const IFactionTownCoordResolver* resolver = nullptr,
                                              ts2::net::NetClient* nc = nullptr);

// Map_BeginWarpToFactionTownDefault 0x55C740 — même table faction->ville, MAIS sans la
// garde "mort" (*(this+1784)!=12) : utilisée quand l'appelant sait déjà que le warp doit
// avoir lieu inconditionnellement (aucun appelant recensé dans le désassemblage — fonction
// probablement destinée à un point d'entrée externe/outil, conservée pour fidélité).
FactionWarpResolution BeginWarpToFactionTownDefault(int32_t element,
                                                     const IFactionTownCoordResolver* resolver = nullptr,
                                                     ts2::net::NetClient* nc = nullptr);

// Map_BeginWarpToMap37 0x55C8A0 — téléportation fixe vers la carte/NPC 37, coordonnées
// EXACTES codées en dur dans le binaire (6.0, 97.0, -3259.0). Seul appelant recensé :
// sub_4A55E0 (trampoline nu, rôle exact non élucidé — probablement une commande outil/
// debug ou un raccourci UI hors périmètre gameplay joueur normal).
FactionWarpResolution BeginWarpToMap37(ts2::net::NetClient* nc = nullptr);

// Warp_SendTeleport 0x5F5CE0 — téléportation par mot-clé/summon vers l'une des 4 zones
// v3[4]={138,139,165,166} (EA 0x5f5ce9/f0/f7/fe). Gardé par (zoneSel<=3 && !g_MorphInProgress,
// EA 0x5f5d1a) ; corps = ArmFullWarp(mode=6, flagA4=1, townNpcId=v3[zoneSel], pos) + Op20(nc,6,
// v3[zoneSel]) (EA 0x5f5d20..0x5f5dd6). `pos` = 3 floats {x,y,z}. Si nc!=nullptr, émet réellement
// le paquet (Net_SendWarpRequest, alias i32 : zoneId >=128 zéro-étendu). Renvoie true si armé.
// Appelé par Warp_ProcessKeyword 0x5F54E0 (12x) et Net_OnSummonSpawn 0x4AA810 (non possédés).
bool Warp_SendTeleport(uint16_t zoneSel, const float* pos, ts2::net::NetClient* nc = nullptr);

} // namespace ts2::game

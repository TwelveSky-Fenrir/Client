// Game/CameraWarpTick.h — 4 morceaux indépendants du tick InGame regroupés par mission :
// caméra 3e personne, timeout "warp supprimé", auto-utilisation de potion, détection de
// guilde/groupe actif. Réécriture C++ fidèle depuis Scene_InGameUpdate 0x52C600 (Hex-Rays
// via idaTs2) et Game_AutoUsePotion 0x5C4800.
//
// Fonctions couvertes (EA d'origine) :
//   - Cam_SetLookAt          0x69CCD0 : cadrage caméra 3e personne (garde-fou d'élévation
//     |asin(dy/dist)| > 89.99deg -> rejeté, caméra inchangée). Appelé une fois à l'entrée en
//     InGame (Scene_InGameUpdate case 3, EA 0x52C6EF..0x52C816 pour le cadrage complet,
//     cf. InGameTickFlowHost::InitCamera dans Game/InGameTickFlow.h).
//   - Timeout du flag "warp supprimé" (dword_1675B00/flt_1675B04), EA exacte 0x52C91F :
//     `if (dword_1675B00 && g_GameTimeSec - flt_1675B04 > 10.0) dword_1675B00 = 0;`
//   - Game_AutoUsePotion 0x5C4800 : auto-utilisation de potion HP puis (si pas de potion HP
//     déclenchée) MP, à chaque frame InGame.
//   - HasPendingTargetRequest (g_PendingReqTargetName_Sub2/Sub1, EA du test 0x52C8E9) :
//     condition qui déclenche Net_SendOp64 toutes les 300 frames, EN MÊME TEMPS que le
//     keepalive Net_SendPacket_Op13 (déjà câblé ailleurs, cf. SceneManager.cpp). RENOMMÉE
//     ET RE-DOCUMENTÉE (audit 2026-07-14, ré-décompilation fraîche de Scene_InGameUpdate) :
//     l'ancien nom "HasActiveGroupName" et sa description "nom de guilde/groupe actif"
//     étaient FAUX — ce N'EST PAS un nom de guilde/groupe persistant. IDA a depuis renommé
//     les deux globals en g_PendingReqTargetName_Sub2 (0x167468A) / _Sub1 (0x1674697) :
//     décompilation de leurs writers confirme qu'il s'agit du NOM DE CIBLE D'UNE REQUÊTE
//     RÉSEAU EN COURS, écrit par Net_OnRequestTargetNameSet (Pkt SC opcode 0x44, sub-op 1/2 ->
//     Sub1/Sub2) et vidé par Net_OnRequestCancelClear (Pkt SC opcode 0x45, poste aussi
//     StrTable005 id 534 "requête annulée"). Lu par UI_ClanWin_OnLUp, UI_NpcMenu_
//     RequestJoinFaction, UI_ClanCreate_Validate, UI_GameHud_On{MouseDown,Click,Render}
//     (0x5D92A0/0x5E5680/0x608780/0x6753E0/0x677160/0x67A3C0) : tout pointe vers un flux
//     UI de requête clan/faction (rejoindre/créer/fenêtre clan), PAS un état de guilde/
//     groupe persistant. Net_SendOp64 (0x4B9B20 — nonce+seq+opcode SEUL, 0 octet de payload
//     utile) toutes les 300 frames tant qu'une requête est en attente ressemble donc à un
//     "keepalive/poll" de requête en vol, pas un "refresh guilde/groupe".
//     CONFIRMATION CROISÉE (mission "CABLAGE ROSTER ALLIANCE/GUILDE", 2026-07-14,
//     Docs/TS2_ALLIANCE_PARTY_ROSTER.md §1) : `game::GroupIdentity` (l'ancien
//     `GameState.h::GroupIdentity{guildName,groupName}` qui mappait CES DEUX MÊMES globals
//     sur "nom de guilde/groupe actif") a été RETIRÉ de GameState.h — il modélisait un
//     concept qui n'existe PAS dans le binaire à cette adresse. Le vrai « nom de MA guilde
//     active » est `game::g_World.allianceRoster.guildName` (== g_LocalGuildName 0x168740C,
//     adresse TOTALEMENT DIFFÉRENTE, alimentée par Net_OnGuildRosterReset/Update
//     0x4a/0x4f — cf. Game/GameState.h::AllianceRoster, Net/GameHandlers_PartyGuild.cpp).
//     IMPACT SI NON CORRIGÉ : un futur câblage qui suivrait l'ancienne doc (brancher ce hook
//     sur un "nom de guilde/groupe actif" quelconque) enverrait Net_SendOp64 dans des
//     conditions FAUSSES (déclenché en permanence tant qu'on est dans une guilde, jamais
//     quand une vraie requête clan est réellement en attente) — donc une régression
//     FONCTIONNELLE si/quand ce hook est un jour câblé pour de vrai (à ce jour
//     host.HasPendingTargetRequest est câblé dans SceneManager.cpp, donc le comportement
//     runtime suit désormais la vérité IDA — cf. section CHANGEMENT SCENEMANAGER.CPP ci-
//     dessous).
//
// CHANGEMENT SCENEMANAGER.CPP APPLIQUÉ (consolidation) :
//   - Game/InGameTickFlow.h/.cpp utilisent désormais les noms `HasPendingTargetRequest` et
//     `SendPendingTargetPoll`.
//   - Scene/SceneManager.cpp lit les deux blobs 0x167468A/0x1674697 depuis g_Client.Blob()
//     et appelle `game::HasPendingTargetRequest(...)` ; Net_SendOp64 reste le poll de
//     requête clan/faction en attente.
//
// Module FEUILLE côté gameplay : ne dépend que de Gfx/Camera.h + STL (ce hook prend
// directement des std::string pour rester autonome, plutôt que d'inclure Game/GameState.h
// pour une seule fonction d'une ligne — l'appelant lit lui-même les deux blobs concernés,
// déjà câblés côté ClientSource, cf. paragraphe CHANGEMENT SCENEMANAGER.CPP APPLIQUÉ ci-
// dessus).
// N'INCLUT PAS Scene/SceneManager.h ni Net/* : tous les effets de bord externes (réseau,
// inventaire/items, stats, timers globaux partagés) sont exposés via des hooks opaques
// (std::function), même modèle que Game/InGameTickFlow.h et Game/AutoPlaySystem.h. Câblage
// réel sur l'existant (Net/SendPackets.h, GameState.h, AutoPlaySystem) laissé à l'agent de
// consolidation (règle de coordination de la mission : ce module n'édite PAS SceneManager.*).
#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include "Gfx/Camera.h"

namespace ts2::game {

// =====================================================================================
// 1. Caméra 3e personne — Cam_SetLookAt 0x69CCD0 + cadrage d'entrée InGame
// =====================================================================================

// Port fidèle de Cam_SetLookAt (thiscall d'origine sur un petit objet caméra à part —
// this+198.. = oeil, this+201.. = cible — non modélisé séparément ici : appliqué
// directement sur `camera`, qui expose le même couple oeil/cible sous une forme
// sphérique isomorphe (cf. Gfx/Camera.h, tête de fichier)).
//
// Rejette (return false, `camera` INCHANGÉE) si :
//   - oeil == cible à l'identique (vecteur de direction nul, 0x69cd07) ;
//   - l'angle d'élévation |asin(dy/dist)| en degrés dépasse 89.989998 (0x69cd8f/91 —
//     confirmé sur le désassemblage brut : fdivp dy/dist -> call Math_AsinFpu -> fmul
//     flt_7BB28C(=57.2957763671875) -> fabs -> fcomp flt_7EDB70(=89.989998)).
// Sinon pose cible/distance/yaw/pitch sur `camera` (Camera::SetTarget/SetDistance/SetYaw/
// SetPitch) et renvoie true. NOTE FIDÉLITÉ : Camera::SetDistance clampe aux bornes de zoom
// [25,150] (Camera_Init) — le binaire d'origine, à cet endroit précis, n'applique PAS ce
// clamp (il écrit l'oeil/cible bruts dans le petit objet caméra ; le clamp de distance est
// un comportement d'UNE AUTRE fonction, Cam_ClampDistance 0x69CE00, invoquée ailleurs par le
// contrôleur d'entrée). Pour le cadrage d'entrée en InGame (distance oeil-cible = ~86.6,
// dans les bornes par défaut) cet écart n'a aucun effet observable ; documenté pour
// fidélité si `camera` a des bornes personnalisées plus étroites.
bool Cam_SetLookAt(gfx::Camera& camera,
                    float eyeX, float eyeY, float eyeZ,
                    float targetX, float targetY, float targetZ);

// État du cadrage caméra 3e personne posé par InGame_InitCamera (dword_1837E64/dword_1837E68
// / g_CamFollowDist d'origine — sémantique exacte des deux flags AU-DELÀ de {1,0} NON
// déterminée dans le désassemblage : ils ne sont plus jamais relus dans Scene_InGameUpdate,
// conservés tels quels par fidélité pour un futur système de rendu/transition caméra).
struct CameraFollowState {
    bool  initialized    = false; // dword_1837E64 : 1 après InitCamera (jamais remis à 0 ici)
    int   transitionFlag = 0;     // dword_1837E68 : remis à 0 par InitCamera
    float followDist     = 0.0f;  // g_CamFollowDist — cf. note fidélité ci-dessous
};

// Scene_InGameUpdate case 3 (EA 0x52C6EF..0x52C816), one-shot à l'entrée en InGame :
//   eye    = self + (50, 60, 50)
//   target = self + (0, 10, 0)
//   Cam_SetLookAt(eye, target) [+ Camera_SetEyeTarget, redondant côté binaire — un seul
//     couple oeil/cible poussé ici, sur `camera`]
//   dword_1837E64 = 1 ; dword_1837E68 = 0
// NOTE FIDÉLITÉ (g_CamFollowDist) : le binaire calcule cette valeur via
// Math_Dist3D(g_CameraPos, flt_80013C) — DEUX globals FIXES du renderer (pas dérivés de la
// position self visible dans cette fonction, probablement l'état caméra PRÉCÉDENT capturé
// avant l'écrasement ci-dessus), inaccessibles depuis ce module feuille (hors périmètre :
// g_GxdRenderer/g_GfxRenderer). On utilise à la place `camera.Distance()` APRÈS application
// du look-at (la grandeur équivalente la plus proche disponible côté ClientSource : le zoom
// orbital de la caméra 3e personne, qui vaut ~86.6 pour ce cadrage précis) — approximation
// assumée, à corriger si g_CamFollowDist s'avère lu ailleurs dans un futur système.
void InGame_InitCamera(gfx::Camera& camera, CameraFollowState& follow,
                        float selfX, float selfY, float selfZ);

// =====================================================================================
// 2. Timeout du flag "warp supprimé" — dword_1675B00/flt_1675B04, EA 0x52C91F
// =====================================================================================

// Miroir minimal de dword_1675B00 (bool "warp supprimé actif") + flt_1675B04 (horodatage de
// pose, g_GameTimeSec au moment de l'armement). INTÉGRATION : dword_1675B00 est le MÊME
// global que AutoPlayExternalState::warpSuppressed (Game/AutoPlaySystem.h), qui n'a
// actuellement PAS de champ d'horodatage — ce module reste donc autonome avec son propre
// état ; l'agent de consolidation devra soit synchroniser les deux bool chaque frame
// (WarpSuppressionState::suppressed -> externalState.warpSuppressed), soit faire porter le
// timestamp par AutoPlayExternalState directement (hors périmètre de cette mission : ce
// fichier n'édite PAS Game/AutoPlaySystem.h).
struct WarpSuppressionState {
    bool  suppressed = false; // dword_1675B00
    float setAtSec   = 0.0f;  // flt_1675B04
};

// EA EXACTE 0x52C91F : auto-clear fidèle si `suppressed` et (gameTimeSec - setAtSec) > 10.0.
// Ne fait rien si `suppressed` est déjà faux (fidèle : le binaire teste `dword_1675B00 &&`
// avant même de lire flt_1675B04).
void Warp_TickSuppressionTimeout(WarpSuppressionState& state, float gameTimeSec);

// Armement du latch — NON désassemblé à l'EA 0x52C91F elle-même (ce site ne fait QUE la
// lecture/auto-clear) : le site d'armement (dword_1675B00=1 ; flt_1675B04=g_GameTimeSec) est
// ailleurs dans le binaire (hors périmètre de cette mission, probablement le même flux qui
// bloque une tentative de warp pendant un cooldown). Fournie comme complément symétrique
// raisonnable pour que `state` soit exploitable de bout en bout ; à faire remplacer par le
// vrai site d'armement si/quand il est reversé.
void Warp_SetSuppressed(WarpSuppressionState& state, float gameTimeSec);

// =====================================================================================
// 3. Auto-utilisation de potion — Game_AutoUsePotion 0x5C4800
// =====================================================================================

enum class PotionKind : uint8_t { Hp, Mp };

// Un emplacement de la ceinture d'auto-play (3 pages x 14 emplacements, dword_1674404 +
// g_Container5_ItemId dans le binaire — conteneur "5", distinct de l'inventaire principal).
struct BeltSlot {
    int page = 0; // 0..2 (i d'origine)
    int slot = 0; // 0..13 (j d'origine)
};

// Points d'intégration externes à ce module — jauges/seuils/ceinture/verrous partagés,
// PROPRIÉTÉ d'autres systèmes (StatEngine, InventorySystem, réseau). Hook nul = valeur par
// défaut documentée (voir Game_AutoUsePotion ci-dessous) : ne bloque jamais silencieusement
// le jeu, désactive juste la fonctionnalité tant que le hook n'est pas branché.
struct AutoPotionHost {
    // Jauges courantes du joueur local (dword_1687370[0]=HP, dword_1687378[0]=MP côté
    // binaire). Correspond à SelfState::hp/mp (Game/GameState.h) côté ClientSource — non lu
    // directement ici pour garder ce module indépendant de GameState.h (l'appelant fait
    // `host.GetHpGauge = [] { return (float)game::g_World.self.hp; };`).
    std::function<float()> GetHpGauge;
    std::function<float()> GetMpGauge;

    // ÉCART DOCUMENTÉ (à vérifier) : le binaire compare les jauges HP/MP ci-dessus à une
    // FRACTION de Char_CalcAttackRatingMin(g_EquipSnapshotScratch) [0x4CD970] pour le seuil
    // HP, et Char_CalcAttackRatingMax(...) [0x4CE3F0] pour le seuil MP — deux agrégats
    // massifs (equip+gemmes+buffs+set bonus+arme...) qui, décompilés intégralement,
    // calculent bel et bien une "note d'attaque" (utilisés ailleurs comme tels dans le
    // binaire), PAS une capacité max HP/MP. Une comparaison HP-vs-note-d'attaque est
    // surprenante mais c'est EXACTEMENT ce que fait Game_AutoUsePotion — reproduit tel
    // quel par fidélité (nommage IDA déjà en place), à la manière des écarts
    // Fx_MeleeSwingTick/Fx_GibUpdate déjà signalés dans Game/InGameTickFlow.h. Correspond à
    // SelfState::atkRatingMin/atkRatingMax (déjà modélisés dans GameState.h).
    std::function<float()> GetHpThresholdMetric; // Char_CalcAttackRatingMin
    std::function<float()> GetMpThresholdMetric; // Char_CalcAttackRatingMax

    // Réglage utilisateur du seuil d'auto-use, 0=désactivé, 1..5 = n/5 de la métrique
    // ci-dessus ; 5 a un sens DIFFÉRENT (bascule vers un test "métrique*0.99" au lieu de
    // "seuil*métrique/5", cf. Game_AutoUsePotion). dword_1674728 (HP) / dword_167472C (MP).
    std::function<int()> GetHpThresholdSetting;
    std::function<int()> GetMpThresholdSetting;

    // Recherche dans la ceinture (3x14) le premier emplacement configuré (type de slot==3,
    // dword_1674404) dont l'item résolu (g_Container5_ItemId -> MobDb_GetEntry(&mITEM,...))
    // a un sous-type ITEM_INFO+340 dans l'ensemble {1,2,5} (kind==Hp) ou {3,4,5} (kind==Mp).
    // Propriété du futur InventorySystem/AutoPlaySystem : un seul hook opaque. Renvoie false
    // si aucun emplacement ne correspond (fidèle : la boucle 3x14 se termine sans déclencher
    // LABEL_70 dans le binaire, l'appelant continue vers le prochain test).
    std::function<bool(PotionKind kind, BeltSlot& out)> FindBeltPotionSlot;

    // Net_SendPacket_Op22(&g_AutoPlayMgr, slot.page, slot.slot) 0x5C4C8E, PUIS armement du
    // cooldown partagé (g_GmCmdCooldownLatch=1 à 0x1675B08, flt_1675B0C=g_GameTimeSec à
    // 0x1675B0C — cf. IsGmCmdCooldownActive ci-dessous, MÊME PAIRE de globals que le latch
    // "un seul parchemin en vol" d'AutoPlaySystem::CheckReturnScroll/CheckTownScroll,
    // Game/AutoPlaySystem.h). L'appelant est responsable d'armer ce cooldown partagé lui-
    // même (Game_AutoUsePotion ne le fait plus séparément, cf. IsGmCmdCooldownActive).
    std::function<void(PotionKind kind, BeltSlot slot)> UsePotion;

    // Verrous partagés avec d'autres systèmes (PAS possédés par ce module) :
    std::function<bool()> IsMorphInProgress;          // g_MorphInProgress == 1 (0x1675A88)
    std::function<bool()> IsAutoPotionSystemEnabled;   // dword_1675D84 (réglage "actif")
    std::function<bool()> IsGmCmdCooldownActive;       // g_GmCmdCooldownLatch (0x1675B08)
    std::function<void()> SetGmCmdCooldownActive;      // posé par UsePotion, exposé séparément
                                                        // pour les appelants qui veulent le
                                                        // faire eux-mêmes (host.UsePotion peut
                                                        // aussi l'appeler en interne).
    std::function<int()>  GetSelfActionState;          // g_SelfActionState[0] (0x1687328)
};

// Game_AutoUsePotion 0x5C4800. À appeler CHAQUE frame InGame (étape 3 du MainTick,
// cf. Game/InGameTickFlow.h). Garde-fous fidèles (return sans effet si) :
//   - GetHpGauge() < 1                        (0x5c485a)
//   - IsMorphInProgress()                     (idem)
//   - !IsAutoPotionSystemEnabled()             (idem)
//   - IsGmCmdCooldownActive()                  (idem)
//   - GetSelfActionState() in {11, 12, 38}     (idem)
// Puis : teste le seuil HP (formule n/5 ou 99%-au-delà-de-5), scanne la ceinture HP
// ({1,2,5}) ; si un emplacement est trouvé -> UsePotion(Hp, slot) et RETOURNE (une seule
// potion par frame). Sinon, MÊME logique pour MP ({3,4,5}).
// Hook nul (non branché) => traité comme "valeur neutre" (jauge/metric à 0 pour les
// float, false/0/-1 pour le reste) : la fonction ne plante jamais, elle désactive juste la
// vérification correspondante.
void Game_AutoUsePotion(const AutoPotionHost& host);

// =====================================================================================
// 4. Cible de requête en attente (clan/faction) — g_PendingReqTargetName_Sub2/Sub1
//    (anciennement unk_167468A/unk_1674697), EA du test 0x52C8E9
// =====================================================================================

// RENOMMÉE (ex HasActiveGroupName) — cf. note de fidélité en tête de fichier (audit
// 2026-07-14) : Crt_Strcmp(&g_PendingReqTargetName_Sub2,"") ||
// Crt_Strcmp(&g_PendingReqTargetName_Sub1,"") — vrai si l'UN des deux emplacements de
// "nom de cible de requête en attente" (0x167468A / 0x1674697) est non vide. Utilisé toutes
// les 300 frames (avec le keepalive Net_SendPacket_Op13) pour décider d'émettre Net_SendOp64
// — cf. InGameTickFlowHost (Game/InGameTickFlow.h, champs à renommer, cf. note de tête).
// CE N'EST PAS un nom de guilde/groupe persistant — GameState.h N'A PLUS de champ
// "GroupIdentity" qui prêtait à confusion sur ces deux globals (retiré par la mission
// "CABLAGE ROSTER ALLIANCE/GUILDE", 2026-07-14) ; le vrai nom de guilde active est
// `game::g_World.allianceRoster.guildName` (adresse DIFFÉRENTE, g_LocalGuildName 0x168740C,
// cf. note de tête de fichier). Les deux globals ci-dessus sont deux emplacements écrits
// par Net_OnRequestTargetNameSet (Pkt SC 0x44) et vidés par
// Net_OnRequestCancelClear (Pkt SC 0x45), lus par l'UI clan/faction (UI_ClanWin_OnLUp,
// UI_NpcMenu_RequestJoinFaction, UI_ClanCreate_Validate, UI_GameHud_*). Paramètres nommés en
// conséquence (reqTargetSub2/reqTargetSub1, PAS guildName/groupName) — ce module reste
// autonome (pas d'include croisé avec GameState.h pour une seule fonction d'une ligne).
bool HasPendingTargetRequest(const std::string& reqTargetSub2, const std::string& reqTargetSub1);

} // namespace ts2::game

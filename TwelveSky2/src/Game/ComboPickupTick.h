// Game/ComboPickupTick.h — Combo/pickup/quête, système d'appoint pour le bloc « étape 12 »
// du tick InGame (Game/InGameTickFlow.h). Réécriture C++ fidèle (traduction réelle du
// désassemblage, pas d'invention) du petit cluster suivant. Vérité = le désassemblage de
// TwelveSky2.exe (imagebase 0x400000) via MCP idaTs2.
//
// Fonctions d'origine traduites (EA -> fonction) :
//   Combo_FindNearbyFollowup     0x501270 -> Combo_FindNearbyFollowup()
//   BeginComboMorph              0x52CF69 -> BeginComboMorph()            (bloc inline de
//                                             Scene_InGameUpdate, PAS une fonction séparée
//                                             dans le binaire — même source que le reste de
//                                             ce fichier, décompilée avec le contexte complet)
//   Combat_IsElementAllowedOnMap 0x55CBF0 -> Combat_IsElementAllowedOnMap()
//   [ramassage 5 emplacements]   0x52CF94..0x52D067 -> TickNearbyPickupSlots() (bloc inline
//                                             de Scene_InGameUpdate, PAS une fonction séparée
//                                             — la boucle des 5 slots + Net_SendOp106)
//   Quest_UpdateMarkerTimer      0x510D90 -> Quest_UpdateMarkerTimer()
//   Tips002_RotateUpdate         0x4C1840 -> Tips_RotateUpdate()          (wrapper : la
//                                             minuterie/rotation d'INDEX est DÉJÀ portée
//                                             fidèlement par TipsTable::Advance, cf.
//                                             Game/StringTables.h — ce fichier ajoute
//                                             uniquement le Msg_AppendChatLine manquant)
//   Npc_AutoInteractForPet       0x53B5F0 -> DÉJÀ PORTÉ intégralement par
//                                             NpcInteractionSystem::AutoInteractForPet()
//                                             (Game/NpcInteraction.h/.cpp) — RÉUTILISÉ tel
//                                             quel, aucune duplication ici (cf. note ci-dessous).
//
// Toutes ces fonctions proviennent du MÊME bloc de code source (le switch/case « étape 12 »
// de Scene_InGameUpdate 0x52C600, cf. Game/InGameTickFlow.h/.cpp pour l'orchestration
// complète du tick). Elles ont été décompilées ensemble (une seule passe Hex-Rays sur
// Scene_InGameUpdate) puis éclatées ici en fonctions/API propres réutilisables.
//
// ---------------------------------------------------------------------------------------
// RÉUTILISATION (règle de la mission — ne PAS dupliquer un système déjà écrit) :
//   - Npc_AutoInteractForPet (0x53B5F0) est déjà intégralement porté par
//     NpcInteractionSystem::AutoInteractForPet() (Game/NpcInteraction.h). Ce fichier ne le
//     réimplémente PAS ; le point d'intégration InGameTickFlowHost::AutoInteractNpcForPet
//     doit appeler CETTE méthode directement (câblage laissé à l'étape de consolidation,
//     cf. RÈGLE CRITIQUE DE COORDINATION de la mission — SceneManager.cpp n'est pas édité ici).
//   - Le ramassage au sol « clic souris » (Item_PickupTarget/Item_InteractGround) est déjà
//     porté par Game/ItemPickupSystem.h — DIFFÉRENT du ramassage AUTOMATIQUE des 5
//     emplacements de proximité ci-dessous (flt_1676130, alimenté par le paquet serveur 0x82,
//     cf. Net/GameHandlers_Misc.cpp), qui n'a pas d'équivalent dans ItemPickupSystem.h : ce
//     fichier l'ajoute (TickNearbyPickupSlots), sans toucher à ItemPickupSystem.h/.cpp.
//   - Quest_CheckObjectiveState / QuestProgressState / QuestStepRecord / LookupQuestStep sont
//     déjà portés par Game/QuestSystem.h — Quest_UpdateMarkerTimer ci-dessous les RÉUTILISE
//     tels quels (n'opère PAS sur un nouveau modèle de données de quête).
//   - ElementPairTable / CombatMorphState (g_SelfMorphNpcId) sont déjà portés par
//     Game/SkillCombat.h — Combat_IsElementAllowedOnMap et BeginComboMorph les RÉUTILISENT.
//   - net::Rng / net::DefaultRng() (Rng_Next 0x7603FD, LCG CRT identique) sont déjà portés
//     par Net/Rng.h — la rotation aléatoire de BeginComboMorph le RÉUTILISE (même génération
//     que tous les builders réseau, fidèle : c'est le MÊME rand() côté binaire).
//   - Net_SendPacket_Op20 / Net_SendOp106 sont déjà déclarés dans Net/SendPackets.h.
//   - TipsTable (Game/StringTables.h) porte déjà le timer/index (600 s) : Tips_RotateUpdate
//     ci-dessous complète juste l'effet de bord manquant (append au journal de chat).
//
// ---------------------------------------------------------------------------------------
// GINFO2 (flt_1555D08, table "motion/combo", ~350 entrées x 805 dwords) : table d'ASSETS
// non modélisée ailleurs dans ClientSource (aucun chargeur identifié pour ce fichier .IMG
// dans le périmètre de cette mission — même situation que la table NPC mQUEST de
// Game/QuestSystem.h, cf. son bandeau "(B) HORS PÉRIMÈTRE"). Combo_FindNearbyFollowup et
// BeginComboMorph consultent CETTE table pour, respectivement, énumérer les candidats de
// suivi de combo proches d'une position, et résoudre la position d'origine d'un combo par
// clé. Les DEUX algorithmes (bornes, boucle, comparaison de distance, ordre des tests) sont
// reproduits fidèlement ci-dessous ; la table elle-même est injectée par l'appelant via
// ComboCandidateLookup / ComboMotionOriginLookup (même patron que QuestStepLookup :
// callback nul -> résultat "aucun candidat"/"position nulle", sûr par défaut).
//
// Combo_CheckTransition (0x4FD650, callee direct de Combo_FindNearbyFollowup) est une table
// de compatibilité combo/niveau/élément de plus de 900 lignes (relève de SkillCombat, HORS
// PÉRIMÈTRE de cette mission précise — seul Combo_FindNearbyFollowup 0x501270 est assigné).
// Injectée via ComboTransitionCheck, même patron.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/QuestSystem.h"
#include "Game/SkillCombat.h"   // ElementPairTable (Char_GetPairedElement), CombatMorphState
#include "Game/StringTables.h" // TipsTable (Tips_RotateUpdate)
#include "Net/NetClient.h"
#include "Net/SendPackets.h"

namespace ts2::game {

// ===========================================================================
// Combat_IsElementAllowedOnMap 0x55CBF0 — matrice élément-vs-carte.
// ===========================================================================
//
// Appel d'origine (confirmé par désassemblage, EA 0x52CF75-0x52CF7A) :
//   mov ecx, offset g_LocalPlayerSheet ; this = fiche du joueur local (Char_GetPairedElement)
//   push g_LocalElement               ; a2   = élément testé (= mapElement ici)
//   call Combat_IsElementAllowedOnMap
// "this" n'est utilisé QUE pour résoudre Char_GetPairedElement(this, a2) : reproduit ici via
// `pairs` (Game/SkillCombat.h::ElementPairTable, même donnée g_LocalPlayerSheet+455..458).
//
// Structure fidèle : switch(mapElement){0,1,2,3} -> switch(paired){-1/soi,0,1,2,3} -> test
// d'appartenance de `selfMorphNpcId` (g_SelfMorphNpcId 0x1675A98, cf.
// Game/SkillCombat.h::CombatMorphState::currentActionId) à un ensemble figé de 8 ou 12 ids.
// NOTE : dans le binaire, les cas "default" internes (goto LABEL_47/92/137/182) sont du code
// mort — Char_GetPairedElement ne renvoie jamais que {-1,0,1,2,3}, valeurs TOUTES couvertes
// explicitement par chaque switch interne. Reproduits ici comme `return false` (jamais
// atteints en pratique, mêmes constantes d'entrée).
bool Combat_IsElementAllowedOnMap(int mapElement, int selfMorphNpcId, const ElementPairTable& pairs);

// ===========================================================================
// Combo_FindNearbyFollowup 0x501270 — cible de combo de suivi à proximité.
// ===========================================================================
//
// int __thiscall Combo_FindNearbyFollowup(_DWORD *this, int a2, int a3)
// {
//   if ( a2 < 1 || a2 > 350 ) return -1;
//   for ( i = 0; i < *(this + 805*a2 - 802); ++i )
//     if ( Math_Dist3D(this + 805*a2 + 3*i - 801, a3) < 30.0
//       && Combo_CheckTransition(a2, *(this + 805*a2 + i - 501)) == 1 )
//       return *(this + 805*a2 + i - 501);
//   return -1;
// }
// this=flt_1555D08 (GINFO2), a2=motionId courant (g_SelfMorphNpcId), a3=&flt_1687330 (self).
// Cf. bandeau GINFO2 en tête de fichier pour l'injection de la table.

// Un candidat de suivi : id de motion + position 3D associée (extrait de GINFO2).
struct ComboMotionCandidate {
    int32_t id = -1;
    float   x = 0.0f, y = 0.0f, z = 0.0f;
};

// Énumère les candidats de suivi liés à `motionId` (GINFO2, non modélisée ici — cf. bandeau).
// nullptr/non branché -> aucun candidat (Combo_FindNearbyFollowup renvoie alors -1, sûr).
using ComboCandidateLookup = std::function<std::vector<ComboMotionCandidate>(int motionId)>;

// Combo_CheckTransition 0x4FD650 (SkillCombat, HORS PÉRIMÈTRE de cette mission) : renvoie
// 1 (transition valide au niveau courant), 2 (transition connue mais niveau insuffisant) ou
// 0 (transition inconnue/hors bornes). Seul le code 1 valide un candidat ici (fidèle :
// `== 1` exact dans le binaire). nullptr/non branché -> 0 constant (aucun candidat valide).
using ComboTransitionCheck = std::function<int(int fromMotionId, int toMotionId)>;

// Renvoie l'id de motion du PREMIER candidat trouvé (ordre de la table) à distance 3D < 30.0
// de (selfX,selfY,selfZ) ET dont la transition (motionId -> candidat.id) vaut exactement 1,
// ou -1 si aucun (ou motionId hors [1,350], garde fidèle EA 0x501286).
int Combo_FindNearbyFollowup(int motionId, float selfX, float selfY, float selfZ,
                              const ComboCandidateLookup& candidates,
                              const ComboTransitionCheck& transitionCheck);

// ===========================================================================
// BeginComboMorph EA 0x52CF69 — démarrage du morph de combo (bloc inline de
// Scene_InGameUpdate, cf. Game/InGameTickFlow.h étape 12c pour le contexte d'appel : appelé
// UNIQUEMENT quand Combo_FindNearbyFollowup a trouvé un candidat ET qu'aucun morph n'est déjà
// en cours — gardes reproduites côté INGameTickFlow_Update, PAS ici).
// ===========================================================================
//
//   g_MorphInProgress = 1;                       // 0x1675A88
//   dword_1675A8C = 4;                            // phase
//   dword_1675A90 = 0;                            // (jamais relu ailleurs dans cette fonction)
//   dword_1675A9C = v24;                          // followupMotionId
//   Crt_Memset(&dword_1675AA0, 0, 72);             // reset bloc warp (72 o)
//   dword_1675AA0 = 0;                             // +0  (redondant avec le memset)
//   dword_1675AA4 = 1;                             // +4  (ÉCRASE le memset -> "armé")
//   flt_1675AA8 = 0.0;                             // +8  (redondant)
//   GInfo2_FindVec3ByKey(flt_1555D08, v24, g_SelfMorphNpcId, &flt_1675AAC); // +12/+16/+20
//   flt_1675AC4 = (float)(Rng_Next() % 360);       // +36 rotation courante
//   flt_1675AC8 = flt_1675AC4;                     // +40 rotation cible (= courante à l'init)
//   Net_SendPacket_Op20(client, dword_1675A8C, v24);

// Bloc warp de 72 o (dword_1675AA0..dword_1675AA0+72 = 0x1675AA0..0x1675AE8), layout fidèle
// aux offsets nommés du binaire ; les zones sans consommateur identifié dans
// Scene_InGameUpdate restent des octets opaques (memset à zéro uniquement, jamais relus ici).
#pragma pack(push, 1)
struct ComboMorphWarpBlock {
    int32_t flag0            = 0;    // +0  dword_1675AA0 (reset, réécrit à 0)
    int32_t flag1            = 0;    // +4  dword_1675AA4 (réécrit à 1 après le memset -> "armé")
    float   timer             = 0.0f; // +8  flt_1675AA8 (reset)
    float   targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f; // +12/+16/+20 (GInfo2_FindVec3ByKey)
    uint8_t _unknown24[12]   = {};    // +24..+35 : zéroés par le memset, aucun consommateur
                                       // identifié dans Scene_InGameUpdate.
    float   rotationCurrent   = 0.0f; // +36 flt_1675AC4 (Rng_Next() % 360)
    float   rotationTarget    = 0.0f; // +40 flt_1675AC8 (== rotationCurrent à l'init)
    uint8_t _unknown44[28]   = {};    // +44..+71 : zéroés par le memset, aucun consommateur
                                       // identifié dans Scene_InGameUpdate.
};
#pragma pack(pop)
static_assert(sizeof(ComboMorphWarpBlock) == 72,
              "ComboMorphWarpBlock doit faire 72 o (cf. Crt_Memset(&dword_1675AA0, 0, 72))");

// État runtime du morph de combo (mirroir g_MorphInProgress/dword_1675A8C/90/9C + bloc warp).
struct ComboMorphState {
    bool                 inProgress       = false; // g_MorphInProgress 0x1675A88
    int32_t              phase            = 0;     // dword_1675A8C (4 une fois démarré)
    int32_t              unk90            = 0;     // dword_1675A90 (toujours 0 ici, jamais relu)
    int32_t              followupMotionId = 0;     // dword_1675A9C
    ComboMorphWarpBlock  warp;
};

// GInfo2_FindVec3ByKey 0x4FD540 (même table GINFO2, cf. bandeau) : résout la position
// d'origine associée à la clé `originKey` (= motionId COURANT, g_SelfMorphNpcId) dans la
// liste du combo `followupMotionId`. `outPos` DOIT être écrit (x,y,z) — {0,0,0} si absent,
// fidèle au binaire qui zéro le vec3 avant recherche et le laisse tel quel si non trouvé.
// nullptr/non branché -> {0,0,0} constant.
using ComboMotionOriginLookup = std::function<void(int followupMotionId, int originKey, float outPos[3])>;

// Démarre le morph de combo vers `followupMotionId` (AUCUNE garde ici : "followup != -1" et
// "!morphInProgress" sont la responsabilité de l'appelant, cf. bandeau ci-dessus et
// Game/InGameTickFlow.cpp). `currentMotionId` = g_SelfMorphNpcId au moment de l'appel (clé
// d'origine passée à GInfo2_FindVec3ByKey). Émet Net_SendPacket_Op20(phase, followupMotionId)
// via `netClient` (Net/SendPackets.h, déjà porté). Rotation aléatoire via net::DefaultRng()
// (Net/Rng.h — MÊME générateur que Rng_Next() d'origine, réutilisé tel quel).
void BeginComboMorph(ComboMorphState& state, int followupMotionId, int currentMotionId,
                      net::NetClient& netClient, const ComboMotionOriginLookup& originLookup);

// ===========================================================================
// Ramassage automatique des 5 emplacements de proximité — bloc inline
// Scene_InGameUpdate EA 0x52CF94..0x52D067 (PAS une fonction séparée dans le binaire).
// ===========================================================================
//
//   for ( nn = 0; nn < 5; ++nn )
//     if ( (flt_1676130[3*nn] || flt_1676130[3*nn+1] || flt_1676130[3*nn+2])
//       && Math_Dist3D(&flt_1676130[3*nn], flt_1687330) < 100.0 )
//     {
//       flt_1676130[3*nn] = flt_1676130[3*nn+1] = flt_1676130[3*nn+2] = 0.0;
//       Net_SendOp106(client, nn, flt_1687330);   // payload = POSITION DU JOUEUR (pas du slot)
//     }
//
// flt_1676130 (15 floats = 5 x vec3) est DÉJÀ alimenté par le handler du paquet serveur 0x82
// (Net/GameHandlers_Misc.cpp, via g_Client.VarF(0x1676130 + i*4)) — ce fichier LIT/ÉCRIT ces
// MÊMES emplacements via g_Client.VarF, sans dupliquer le stockage.
inline constexpr int      kNearbyPickupSlotCount   = 5;
inline constexpr float    kNearbyPickupRadius       = 100.0f; // EA 0x52d023 (< 100.0 strict)
inline constexpr uint32_t kNearbyPickupSlotBaseAddr = 0x1676130u; // flt_1676130 (g_Client.VarF)

// Efface + Net_SendOp106 pour chaque emplacement non nul à distance 3D < 100.0 de
// (selfX,selfY,selfZ). Payload réseau = position du JOUEUR local (fidèle : PAS la position
// du slot, cf. Net_SendOp106(client, nn, flt_1687330) dans le binaire).
void TickNearbyPickupSlots(float selfX, float selfY, float selfZ, net::NetClient& netClient);

// ===========================================================================
// Quest_UpdateMarkerTimer 0x510D90 — minuterie d'affichage du marqueur de quête (30 s/600 s).
// ===========================================================================
//
// Opère sur le MÊME struct g_PlayerCmdController (0x1669170) que Game/QuestSystem.h ::
// QuestProgressState (zoneId +10249*4=40996, npcQuestId +11553*4=46212 — RÉUTILISÉS tels
// quels, PAS un nouveau modèle) + 5 champs propres à CETTE fonction (+51576..+51592, hors
// périmètre de QuestSystem.h -> QuestMarkerState ci-dessous, même patron que
// NpcInteraction.h::NpcInteractionExt : struct parallèle, pas un overlay mémoire).
struct QuestMarkerState {
    bool     active            = false; // +51576 dword_this+51576 (marqueur affiché)
    float    lastTimerSec       = 0.0f;  // +51580 (réutilisé pour les DEUX minuteries 30s/600s)
    int32_t  lastObjectiveState = 0;    // +51584 (résultat mis en cache de Quest_CheckObjectiveState)
    uint32_t targetNpcQuestKey  = 0;    // +51588 (champ "+92" == QuestStepRecord::field92 du
                                         //         record NPC mQUEST résolu, cf. QuestSystem.h)
    int32_t  markerVariant      = 0;    // +51592 (0 pour le cas "objectif rempli" v2==1 ;
                                         //         Rng_Next()%3+1 pour le cas "en cours")
};

// g_WarehouseWindowOpen && dword_1822ED0 && *dword_1822ED0 == targetNpcQuestKey — état de
// fenêtre entrepôt/quête ouverte (UI, HORS PÉRIMÈTRE de ce fichier). Appelé DEUX FOIS avec
// des clés DIFFÉRENTES dans le binaire (une fois contre le marqueur déjà armé, une fois
// contre le NOUVEAU candidat résolu) : callback plutôt qu'un bool figé, pour rester fidèle.
// nullptr/non branché -> false constant (aucune fenêtre ne "consomme" jamais le marqueur).
using WarehouseTargetMatch = std::function<bool(uint32_t targetNpcQuestKey)>;

// Snd3D_PlayScaledVolume 0x4DA380 (audio, HORS PÉRIMÈTRE) : joué UNIQUEMENT dans la branche
// v2==1 ("objectif rempli", cf. binaire EA 0x510e80). nullptr/non branché -> silencieux.
using QuestMarkerSoundCallback = std::function<void()>;

// `gameTimeSec` = g_World.gameTimeSec. `isArenaZone` = Map_IsArenaZone(&unk_1685740) 0x54B690
// (garde globale de tête de fonction, HORS PÉRIMÈTRE — non modélisée ailleurs dans
// ClientSource ; true -> la fonction ne fait RIEN, fidèle).
void Quest_UpdateMarkerTimer(QuestMarkerState& marker, const QuestProgressState& progress,
                              float gameTimeSec, bool isArenaZone,
                              const WarehouseTargetMatch& warehouseTargetMatches,
                              const QuestMarkerSoundCallback& playMarkerSound = nullptr);

// ===========================================================================
// Tips002_RotateUpdate 0x4C1840 — rotation des astuces (bandeau d'annonces mGAMENOTICE).
// ===========================================================================
//
// Le timer/index (600 s, wrap à 0) est DÉJÀ porté fidèlement par TipsTable::Advance
// (Game/StringTables.h) — CE fichier ajoute uniquement l'effet de bord manquant :
//   Msg_AppendChatLine((char*)this + 101*currentIndex + 4, 3, &String); // 0x4c18c6
// c.-à-d. append du nouveau texte d'astuce au journal de chat (canal littéral "3" du
// binaire — NOTE FIDÉLITÉ : la résolution exacte ARGB de ce canal n'est pas prouvée ici,
// cf. même limitation que g_SysMsgColor documentée dans NpcInteraction.cpp ; la valeur
// littérale 3 est transmise telle quelle à MessageLog::Chat en attendant la table de
// couleurs de canal réelle).
void Tips_RotateUpdate(TipsTable& tips, float gameTimeSec);

} // namespace ts2::game

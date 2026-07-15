// Game/GroundAuraWorldObjectTick.h — SYSTEME EFFETS AU SOL / AURAS (PROJECTILES) / OBJETS
// DE ZONE : réécriture C++ des 3 groupes de hooks laissés en TODO par
// Game/InGameTickFlow.h (étapes 7, 10, 11 de RunMainTick, cf. Game/InGameTickFlow.cpp),
// source unique de vérité = décompilation Hex-Rays via idaTs2 (imagebase 0x400000) +
// Docs/TS2_FX_CATALOG.md (catalogue des 44 fonctions Fx_*). Module Game/*.h/.cpp AUTONOME
// (mission dédiée "effets au sol/auras/objets de zone") : n'édite PAS Scene/SceneManager.*
// ni App/App.* — l'agent de consolidation câble les hooks InGameTickFlowHost ci-dessous
// sur l'existant (même politique que Game/EntityLifecycleTick.h).
//
// ===========================================================================================
// 1. Fx_MeleeSwingTick 0x5803A0 (+ Fx_MeleeSwingTick_Loop 0x580400 / _Once 0x5804A0)
// ===========================================================================================
// Étape 7 de Game/InGameTickFlow.h (host.TickGroundItemEffect). Malgré son nom IDA
// ("traînée de coup d'arme mêlée", cf. Docs/TS2_FX_CATALOG.md §2), CETTE fonction est
// appelée depuis Scene_InGameUpdate 0x52C600 sur le tableau dword_1764D18 (=dword_1764D14+4
// dwords, stride 22 dw = 88 o), dont Docs/TS2_PROTOCOL_SPEC.md §[SC b00] confirme
// explicitement l'identité : "dword_1764D14 ... sub_57FE70 = ground items" — c'est le
// tableau OBJETS AU SOL (game::GroundItem, Game/GameState.h), PAS un tableau de
// personnages/joueurs. Écart de nommage IDA confirmé par décompilation (comme documenté
// dans Game/EntityLifecycleTick.h pour sub_55D720/580550/583390) : la variable de boucle
// "g_NpcCount" (0x1687220) qui borne ce tableau dans Scene_InGameUpdate est ELLE AUSSI mal
// nommée — Docs/TS2_PROTOCOL_SPEC.md la documente comme le compteur du pool "ground items"
// (dword_1764D14), sans rapport avec le VRAI tableau NPC (dword_17AB534/dword_1687228, cf.
// Game/GameState.h::NpcEntity). Un objet posé au sol a donc un petit minuteur d'effet
// visuel (boucle ou une-fois) probablement lié à un "glint"/scintillement — sémantique de
// haut niveau non confirmée, mais le MÉCANISME (dispatch loop/once, avance de frame,
// culling de distance à 400 u) est reproduit fidèlement ci-dessous.
//
// *** MISE À JOUR (audit étapes 5-8, 2026-07-14) — le paragraphe ci-dessus est OBSOLÈTE ***
// Re-décompilation fraîche de sub_5803A0/580400/5804A0 : l'IDB a depuis été renommé
// Npc_RenderSlotTick / Npc_RenderSlotTick_Loop / Npc_RenderSlotTick_Once, le tableau
// dword_1764D14 est désormais nommé g_NpcRenderArray, ET le commentaire de tête généré par
// une passe d'analyse plus récente affirme EXPLICITEMENT : « PAS un tableau d'objets au sol
// (contrairement à l'hypothèse ClientSource 'GroundItem') -- confirmé par
// Scene_PickNpcAtScreen/World_PickEntityAtCursor/UI_GameHud_Render qui lisent le même
// tableau comme NPCs ». Vérifié indépendamment ici par 3 traces croisées supplémentaires :
//   - Scene_PickNpcAtScreen 0x541280 (raycast écran -> index dans g_NpcRenderArray via
//     Scene_RayHitNpcBox 0x541680, nom explicite "pick NPC at screen").
//   - Item_PickupTarget 0x539EC0 (nommée "pickup ground item" mais dont le SEUL effet
//     observable est `UI_NpcWin_Open(&g_NpcRenderArray[22*idx])` -- ouvre une fenêtre de
//     dialogue NPC, pas une UI de ramassage d'objet).
//   - Pkt_EnterWorld 0x464160 : le destructeur de slot appelé sur ce tableau au reset de
//     zone est maybe_cGameData_ListField1ItemDtor 0x57FE70 (même famille de nommage que les
//     destructeurs NPC/monstre voisins dans la même fonction).
// CONCLUSION : dword_1764D14/g_NpcRenderArray est très probablement un tableau de NPCs
// (avec zone de clic 3D + fenêtre de dialogue), PAS des objets au sol au sens gameplay —
// ce qui contredirait la classification GroundItem de Game/GameState.h (hors périmètre
// d'édition de cette mission, non modifiée ici). Le paragraphe original ci-dessous est
// conservé tel quel pour l'historique/traçabilité de la décision précédente, mais sa
// conclusion ("objets au sol") doit être considérée COMME PÉRIMÉE. Le MÉCANISME reproduit
// (dispatch loop/once par ext.mode, avance de frame, culling à 400 u) reste correct et
// n'est PAS affecté par cette révision sémantique : aucun changement de code nécessaire
// dans GroundAuraWorldObjectTick.cpp, uniquement une correction de documentation.
//
// Layout du record (indices dword depuis le pointeur `this` d'origine = &record + 4 o,
// donc `this+N` = record octet 4+4N) :
//   this+0  (record+4)  : pointeur, déréférencé puis +1324 -1 avant Model_GetWeaponEffectFrameCount
//                          — HORS PÉRIMÈTRE (donnée d'asset/modèle), jamais résolu ici.
//   this+1  (record+8)  : flag actif (garde de tête, redondant avec GroundItem::active).
//   this+3  (record+16) : mode (0=Loop, 1=Once, autre=no-op) — RÉUTILISÉ tel quel comme
//                          2e paramètre ("variant") de Model_GetWeaponEffectFrameCount.
//   this+4  (record+20) : frame courante (float, avance de dt*30/s).
//   this+5..7 (record+24..32) : position xyz — mappée sur GroundItem::x/y/z (même
//                          convention que les autres tableaux d'entités, cf. GameState.h).
//   this+11 (record+48) : champ destination (sémantique NON déterminée).
//   this+20 (record+84) : champ source, copié dans this+11 quand la distance au joueur
//                          local dépasse 400 u (sémantique NON déterminée — probablement un
//                          "snapshot" figé de LOD/culling, non résolu par cette mission ;
//                          reproduit MÉCANIQUEMENT, pas inventé).
//
// GroundItem (Game/GameState.h) est un modèle PROPRE volontairement allégé qui ne porte PAS
// ces champs de tick par-frame — à l'image de MonsterTickExt/NpcTickExt
// (Game/EntityLifecycleTick.h), ils vivent dans une structure d'EXTENSION parallèle
// (GroundItemTickExt) indexée comme g_World.groundItems, PAS dans GameState.h (fichier
// socle partagé, hors périmètre d'édition de cette mission).
//
// ===========================================================================================
// 2. Pool de projectiles d'attaque (g_FxAuraCount 0x168722C / dword_17D06F4)
// ===========================================================================================
// Étape 10 de Game/InGameTickFlow.h (host.GetFxAuraCount / IsFxAuraActive /
// UpdateHomingProjectile). Identification CONFIRMÉE (mission "aura/objets-de-monde",
// 2026-07-14, cf. commentaires Game/AnimationTick.h/InGameTickFlow.h/MiscManagers.cpp) :
// g_FxAuraCount N'EST PAS un pool de buffs/auras — c'est le compteur du pool SoA de
// PROJECTILES D'ATTAQUE dword_17D06F4 (stride 64 dw), alloué par
// Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10, mis à jour par
// Fx_HomingProjectileUpdate 0x5862D0 (2497 instructions — trajectoire en arc, collisions,
// notification réseau Op18, rendu — HORS PÉRIMÈTRE de cette mission, cf.
// Docs/TS2_FX_CATALOG.md §2). AUCUN conteneur SoA n'existe côté ClientSource à ce jour :
// conformément à la consigne de mission ("NE PAS l'inventer si absent, juste documenter"),
// les 3 fonctions ci-dessous sont un câblage MINIMAL et SÛR (ne plantent jamais, ne
// prétendent pas modéliser le pool) — PAS une réécriture de Fx_HomingProjectileUpdate.
//
// ===========================================================================================
// 3. Objets de zone / nœuds de ressource (dword_1687230 / dword_180EEF4)
// ===========================================================================================
// Étape 11 de Game/InGameTickFlow.h (host.GetWorldObjectCount / IsWorldObjectActive /
// TickWorldObject). Conteneur DÉJÀ modélisé : Game/GameState.h::ZoneObjectEntity,
// g_World.zoneObjects, redimensionné à 500 par GameData_InitPools() (Game/MiscManagers.cpp).
// Le tick d'origine (sub_584170 0x584170, appelé pour chaque objet actif) décompile en un
// STUB __stdcall VIDE (corps `;`, AUCUN effet observable) — reproduit ici fidèlement tel
// quel : "logique de tick simple, aucune logique complexe connue" (confirmé par
// décompilation, pas une supposition).
//
// *** VÉRIFICATION EN CONDITIONS RÉELLES (mission audit chaîne réseau -> gameplay,
// 2026-07-14) *** : à l'écriture initiale de ce module, Net/GameHandlers_BossWorld.cpp
// enregistrait bien Pkt_SpawnZoneObject sur l'opcode 0x86, MAIS le handler était un TODO
// stub (`(void)p;`) qui n'écrivait JAMAIS dans g_World.zoneObjects — RUPTURE DE CHAÎNE
// CONFIRMÉE : le pool restait figé à 500 slots `active=false` de bout en bout, donc
// GetWorldObjectCount() renvoyait 500 (capacité fixe, correct) mais IsWorldObjectActive()
// renvoyait TOUJOURS false et TickWorldObject() n'était jamais atteint sur un slot réel.
// CORRIGÉ ICI (même mission) : Net/GameHandlers_BossWorld.cpp implémente désormais l'upsert
// par (idHi,idLo)/action fidèle à RE/net_handler_notes.md (## Pkt_SpawnZoneObject, op 0x86) —
// action==2 crée/rafraîchit un slot (recherche par id, sinon 1er slot libre, PAS
// d'agrandissement au-delà de 500), action==3 libère le slot et RAZ la cible auto verrouillée
// si elle pointait ce slot (dword_1675B24==7 && dword_1675B28==index, cf.
// Game/AutoTargetCombatGate.h). Ce fichier (GetWorldObjectCount/IsWorldObjectActive/
// TickWorldObject) n'a nécessité AUCUNE modification : il lisait déjà correctement
// g_World.zoneObjects, seul le producteur réseau était manquant.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "Game/GameState.h"

namespace ts2::game {

// ===========================================================================
// 1. Fx_MeleeSwingTick — extension "objet au sol"
// ===========================================================================

// Champs de tick par-frame absents de GroundItem (cf. commentaire de tête). Indexé comme
// g_World.groundItems (grandit paresseusement, même politique que MonsterTickExt/NpcTickExt
// dans Game/EntityLifecycleTick.h).
struct GroundItemTickExt {
    uint32_t effectDefHandle = 0; // this+0 déréférencé — opaque, HORS PÉRIMÈTRE (asset), jamais
                                   // peuplé par ce module (aucun système d'assets/items ne
                                   // l'alimente encore côté ClientSource).
    int32_t  mode  = 0;           // this+3 (record+16) : 0=Loop, 1=Once, autre=no-op.
    float    frame = 0.0f;        // this+4 (record+20).
    float    farField44 = 0.0f;   // this+11 (record+48) — sémantique non déterminée.
    float    farSrcField80 = 0.0f;// this+20 (record+84) — sémantique non déterminée, copiée
                                   // dans farField44 quand distance au joueur local > 400 u.
};

// Stockage d'extension, indexé comme g_World.groundItems. Grandit paresseusement.
inline std::vector<GroundItemTickExt> g_GroundItemTickExt;

// Réinitialise l'extension d'un slot (à appeler par l'agent de consolidation depuis le futur
// point de spawn/pickup d'objet au sol, quand un slot recyclé change d'identité réseau —
// même politique que Game/EntityLifecycleTick.h::ResetMonsterTickExt/ResetNpcTickExt).
// No-op si l'index est hors bornes (agrandit d'abord si besoin).
void ResetGroundItemTickExt(int groundItemIndex);

// Callback opaque vers Model_GetWeaponEffectFrameCount 0x4E5A40 (HORS PÉRIMÈTRE — table de
// modèles/assets). `effectDefHandle` = GroundItemTickExt::effectDefHandle (toujours 0
// aujourd'hui, cf. ci-dessus) ; `variant` = GroundItemTickExt::mode (même champ que le
// dispatch loop/once, double usage fidèle à l'original). nul, ou retour <= 0 -> le timer
// avance mais NE boucle/complète JAMAIS (même politique de dégradation que
// Game/AnimationTick.h::IMorphModelOracle).
struct GroundAuraWorldObjectTickHost {
    std::function<int(uint32_t effectDefHandle, int32_t variant)> GetWeaponEffectFrameCount;
};

// Fx_MeleeSwingTick 0x5803A0 (dispatch) + Fx_MeleeSwingTick_Loop 0x580400 / _Once 0x5804A0
// (fusionnées ici, dispatch fidèle par ext.mode). Étape 7 de Game/InGameTickFlow.h
// (host.TickGroundItemEffect = ce hook, signature déjà alignée :
// `[](int idx, float dt){ TickGroundItemEffect(g_World, idx, dt, host); }`).
// Appelée uniquement quand world.groundItems[index].active (garde déjà faite par
// l'appelant, cf. InGameTickFlow.cpp ~ligne 64) ; revérifiée ici par robustesse défensive
// (fidèle au garde `*(this+1)` de tête de fonction @0x5803AC). No-op si index hors bornes.
void TickGroundItemEffect(GameWorld& world, int groundItemIndex, float dt,
                           const GroundAuraWorldObjectTickHost& host);

// ===========================================================================
// 2. Pool de projectiles d'attaque (g_FxAuraCount / dword_17D06F4) — cf. commentaire de tête
// ===========================================================================

// g_FxAuraCount (0x168722C). Lit le VRAI compteur via l'échappatoire longue traîne
// (Game/ClientRuntime.h::g_Client.Var, clé = adresse d'origine) plutôt qu'un stub renvoyant
// 0 en dur : dès qu'un futur système de spawn de projectiles écrira
// `game::g_Client.Var(0x168722C) = n;` (miroir de `*(this+1721) = n` dans
// cGameData_InitPools/Fx_SpawnAttackProjectile), cet accesseur reflétera la vraie valeur
// SANS modification ici. Vaut 0 aujourd'hui (rien ne peuple encore ce slot côté
// ClientSource) — c'est le VRAI état actuel du jeu réécrit, pas une valeur inventée.
int GetFxAuraCount();

// dword_17D06F4[64*index] (état du slot, 1er dword = actif). Le pool SoA de projectiles
// N'EST PAS modélisé en C++ (aucun tableau équivalent dans GameState.h/ClientRuntime.h) :
// impossible de lire un état de slot réel sans l'inventer (consigne explicite de mission).
// Renvoie TOUJOURS false (repli sûr et documenté, PAS un TODO caché) — en pratique jamais
// atteint tant que GetFxAuraCount()==0 (cas actuel : la boucle étape 10 est bornée par
// GetFxAuraCount(), cf. Game/InGameTickFlow.cpp).
bool IsFxAuraActive(int index);

// Fx_HomingProjectileUpdate 0x5862D0 (2497 instructions, 326 blocs — trajectoire en arc,
// collisions, notification réseau Op18, rendu/son — HORS PÉRIMÈTRE de cette mission, cf.
// Docs/TS2_FX_CATALOG.md §2 pour le détail). No-op DOCUMENTÉ : ne PLANTE jamais, ne fait
// rien tant que le pool SoA réel n'existe pas côté ClientSource (cf. IsFxAuraActive
// ci-dessus — jamais atteint en pratique aujourd'hui). Présent uniquement pour compléter la
// signature du hook host.UpdateHomingProjectile.
void UpdateHomingProjectile(int index, float dt);

// ===========================================================================
// 3. Objets de zone / nœuds de ressource (g_World.zoneObjects) — cf. commentaire de tête
// ===========================================================================

// dword_1687230 == g_World.zoneObjects.size() (capacité fixe post-init, 500 — cf.
// GameData_InitPools, Game/MiscManagers.cpp). 0 si le pool n'a pas encore été initialisé.
int GetWorldObjectCount(const GameWorld& world);

// dword_180EEF4[19*index] (état actif, +0x00 de ZoneObjectEntity). false si index hors
// bornes.
bool IsWorldObjectActive(const GameWorld& world, int index);

// sub_584170 0x584170 : STUB __stdcall VIDE dans le binaire d'origine (corps `;`, aucun
// effet observable, confirmé par décompilation Hex-Rays — PAS une supposition). Reproduit
// fidèlement tel quel : cette fonction ne fait délibérément RIEN. Présente uniquement pour
// compléter la signature du hook host.TickWorldObject (fidèle : PAS d'indice passé à
// l'original, cf. Game/InGameTickFlow.h).
void TickWorldObject(float dt);

} // namespace ts2::game

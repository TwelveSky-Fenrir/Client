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
// ex-VeryOldClient: EFFECT_OBJECT::Update (FSM mObjType 1..14) — CONFIRMED (Docs/TS2_FX_ROSETTA.md
// §1). Build EU SANS mega-struct EFFECT_OBJECT[1000] (CONFLICT 3-A, IDA gagne) : les timers de
// swing sont portés ici ; slots d'attache (Fx_Attach*) et pool SoA projectiles sont séparés.
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
// g_FxAuraCount N'EST PAS un pool de buffs/auras — c'est la CAPACITÉ (1000, cf.
// cGameData_InitPools @0x5575DC) du pool SoA UNIFIÉ de FX dword_17D06F4 (stride 64 dw =
// 256 o/slot). Un slot porte soit un PROJECTILE d'attaque (états FSM 1..4/12..13), soit un
// effet d'attache (états 5..11/14, pool #2 render, NON POSSÉDÉ). Ce module possède le
// sous-ensemble PROJECTILE alloué par Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10
// (états 3→4) et le tick FSM partagé Fx_HomingProjectileUpdate 0x5862D0 pour CES états.
//
// IMPLÉMENTÉ (mission « FX pool #1 » — remplace l'ancien câblage vide) : le layout du slot
// (64 dw, ancré offset par offset dans le .cpp), l'allocation (spawn), et le tick homing des
// états 3 (arc parabolique weaponId==113 / homing direct Alt / homing vers entité vivante) et
// 4 (anim d'impact) sont portés FIDÈLEMENT (trajectoire, arrivée, transition d'état, payload
// de rapport Op18). Tables arme→motion (Anim_MapWeaponToMotion1/2/3 0x5475F0/547970/547CF0) et
// helpers math (Math_MoveProjectileArc 0x588640, Math_AngleBetween2D 0x53FB20, Math_Dist3D
// 0x53FAA0) portés inline. Les effets HORS PÉRIMÈTRE (anim d'impact via
// ModelObj_GetSubObjectCount, rapport réseau Op18, son positionnel, immunité élémentaire via
// g_LocalPlayerSheet non modélisée) sont différés via g_FxProjectileHost (callbacks nuls =
// dégradation sûre : le projectile vole et transitionne fidèlement, seuls les effets de bord
// sont différés). Les états 1/2/12/13 (projectiles d'AUTRES spawns non possédés) et 5..11/14
// (pool #2 render) sont ignorés (no-op fidèle `default:` @0x5862D0).
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
// modèles/assets). ex-VeryOldClient: EFFECT_OBJECT.mFrame (borné par le nb de frames du mesh MOB
// via Model_GetNpcMeshSlot 0x4E5910 → Motion_GetFrameCount 0x4D7830) — CONFIRMED
// (Docs/TS2_FX_ROSETTA.md §1). `effectDefHandle` = GroundItemTickExt::effectDefHandle (toujours 0
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
// 2. Pool de projectiles d'attaque FX (g_FxAuraCount / dword_17D06F4) — cf. commentaire de tête
// ===========================================================================
// IMPLÉMENTÉ (mission « FX pool #1 », Règle #0 : ancres IDA dans le .cpp). Pool SoA unifié
// dword_17D06F4 (base 0x17D06F4, stride 64 dw = 256 o/slot, capacité g_FxAuraCount
// 0x168722C = 1000). Spawn Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10 → état 3 ; tick
// FSM Fx_HomingProjectileUpdate 0x5862D0 (états 3→4 possédés ; 1/2/12/13 = autres spawns non
// possédés ; 5..11/14 = attache render, pool #2 non possédé). ex-VeryOldClient: EFFECT_OBJECT
// types 1→2 / 3→4 / 12→13 — PLAUSIBLE (taxonomie seule ; SoA stride 64 EU ≠
// EFFECT_OBJECT[1000], CONFLICT 3-A). Effets de bord render/net/audio/feuille-élément différés
// via g_FxProjectileHost (ci-dessous).
// ===========================================================================

// Champs lus par Fx_SpawnAttackProjectile @0x582530 sur son `this` = un enregistrement
// MONSTRE dword_1766F74 (l'appelant Char_Update 0x581E10 passe le monstre en tick d'attaque).
// L'appelant du câblage (Char_Update → spawn, mission séparée) remplit cette structure depuis
// le monstre + son MONSTER_INFO (`this+96`). Offsets d'origine (octets) en commentaire.
struct FxProjectileSpawnParams {
    EntityId owner;             // caller+4 / caller+8 (id du tireur)          @0x5825B5/0x5825C7
    EntityId target;            // caller+68 / caller+72 (id de la cible)      @0x582601/0x582613
    float    startX = 0.0f;     // caller+32                                   @0x58281F
    float    startYRaw = 0.0f;  // caller+36 (avant ajout de heightOffset)     @0x58283D
    float    startZ = 0.0f;     // caller+40                                   @0x58284F
    float    targetX = 0.0f;    // caller+44                                   @0x5828D7
    float    targetY = 0.0f;    // caller+48                                   @0x5828E9
    float    targetZ = 0.0f;    // caller+52                                   @0x5828FB
    float    heading = 0.0f;    // caller+56 (cap initial, deg)                @0x58286F
    uint32_t weaponId = 0;      // (*(caller+96))+244 (id arme/skill)          @0x5825DF
    int32_t  weaponSubtype = 0; // (*(caller+96))+236 (switch élément wep113)  @0x58268F
    int32_t  heightOffset = 0;  // (*(caller+96))+328 (ajouté à startYRaw)     @0x58283D
    int32_t  speed = 0;         // (*(caller+96))+332 (vitesse, int→float)     @0x582913
};

// Miroir du payload Op18 (this+180 = slot dw[45..56]) passé à Net_SendPacket_Op18
// (&g_AutoPlayMgr, this+180) @0x4B4CF0. Rempli à l'impact ; l'émission réseau réelle est
// faite par le host (HORS PÉRIMÈTRE réseau).
struct FxImpactReport {
    int32_t  type = 4;          // dw[45] = 4                                  @0x58291F
    EntityId owner;             // dw[46]/dw[47] (tireur)                      @0x582935/0x582947
    EntityId target;            // dw[48]/dw[49] (= id du joueur local à l'impact)
    float    impactX = 0.0f, impactY = 0.0f, impactZ = 0.0f; // dw[50..52] (pos du joueur local)
    int32_t  flag1 = 1, flag2 = 0, flag3 = 0;                // dw[53..55]
    int32_t  homing = 0;        // dw[56] (0 = spawn normal, 1 = Alt)
};

// Effets de bord HORS PÉRIMÈTRE du tick FX (render/net/audio/feuille-élément). Callback nul =
// no-op sûr (le pool fonctionne : spawn/vol/transition restent fidèles). Peuplé par l'agent de
// consolidation ; EA d'origine en commentaire.
struct FxProjectileHost {
    // ModelObj_GetSubObjectCount(&unk_B551B8 + 148*motionIndex, 0) 0x4D7080 : nb de frames du
    // modèle de vol/impact du projectile. <=0 (défaut) → pas de gating d'anim : l'état 4 se
    // termine immédiatement (le projectile disparaît sans jouer l'anim d'impact ; trajectoire
    // et rapport restent fidèles). @0x58659F (état3) / @0x58717D (état4)
    std::function<int(int motionIndex)> GetProjectileFrameCount;
    // Net_SendPacket_Op18(&g_AutoPlayMgr, slot+180) 0x4B4CF0 : rapport de hit auto (émis quand
    // la cible du projectile est le joueur local). Nul → pas de rapport. @0x5867B6/@0x5869E2
    std::function<void(const FxImpactReport&)> NotifyProjectileImpact;
    // Snd3D_PlayPositional(&flt_1487CBC[48*soundId], .., pos, self, 1) 0x4DA450 : son d'impact
    // positionnel (soundId = Anim_MapWeaponToMotion3(weaponId)). Nul → silencieux. @0x586A1E
    std::function<void(int soundId, float x, float y, float z)> PlayImpactSound;
    // dw[10] (elemImmune), calculé au spawn pour weaponId==113 : g_LocalElement (0x1673194 =
    // Game/GameState.h self.element) + Char_GetPairedElement (0x557C00) sur g_LocalPlayerSheet
    // (0x1685748, feuille NON modélisée). true supprime le rapport Op18 d'auto-hit (immunité
    // élémentaire). Nul → false (jamais immunisé, cas courant). Switch fidèle (weaponSubtype) :
    //   0x12→false ; 0x23→ !elem||elem==paired(0) ; 0x24→ elem==1||elem==paired(1) ;
    //   0x25→ elem==2||elem==paired(2) ; 0x26→ elem==3||elem==paired(3).  @0x58268F..0x5827A6
    std::function<bool(int weaponSubtype)> IsLocalElementImmune;
};

// Host FX partagé (peuplé par l'agent de consolidation — mission séparée). Nul par défaut :
// le pool fonctionne (spawn/vol/transition) sans rendu/réseau/audio.
inline FxProjectileHost g_FxProjectileHost;

// cGameData_InitPools 0x5575D0 (pool FX) : fixe g_FxAuraCount=1000 (@0x5575DC) et RAZ les slots
// (actif=0). À appeler à l'entrée en monde (Pkt_EnterWorld @0x4642A4 fait la même boucle de
// clear). Idempotent ; la capacité par défaut vaut déjà 1000 sans cet appel.
void Fx_InitProjectilePool();

// Fx_SpawnAttackProjectile 0x582530 : alloue le 1er slot libre en état 3 (homing) depuis `p`.
// Retour = (index<<8) du slot alloué, ou g_FxAuraCount si le pool est plein (fidèle : `return
// i` sans alloc). Si Anim_MapWeaponToMotion1(weaponId)==-1 → slot libéré, retour (index<<8).
// Faces homing : weaponId==113 → arc parabolique ; sinon homing vers l'entité vivante ciblée.
int Fx_SpawnAttackProjectile(const FxProjectileSpawnParams& p);
// Fx_SpawnAttackProjectileAlt 0x582A10 : variante « homing direct vers cible fixe » (dw[12]=1,
// dw[56]=1 ; PAS de branche weaponId==113). Même allocation/retour.
int Fx_SpawnAttackProjectileAlt(const FxProjectileSpawnParams& p);

// g_FxAuraCount (0x168722C) — CAPACITÉ du pool (borne de la boucle du tick étape 10, cf.
// Game/InGameTickFlow.cpp). = 1000 (cGameData_InitPools @0x5575DC).
int GetFxAuraCount();

// dword_17D06F4[64*index] (1er dw du slot = actif). Garde d'index @0x52CB8F (boucle du tick).
bool IsFxAuraActive(int index);

// Fx_HomingProjectileUpdate 0x5862D0 : tick FSM d'un slot. Implémente FIDÈLEMENT les états 3
// (vol homing : arc wep113 / homing direct Alt / homing vers entité vivante) et 4 (anim
// d'impact). Les états 1/2/12/13 (projectiles d'AUTRES spawns NON POSSÉDÉS :
// Effect_SpawnSkillProjectile 0x573A90, …) et 5..11/14 (attache/particules, pool #2 render NON
// POSSÉDÉ) ne sont pas produits par ce module → ignorés (no-op sûr, `default: return` fidèle).
// Guard d'index + garde d'actif de tête @0x5862D6.
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

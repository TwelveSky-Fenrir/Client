// Game/GameState.h — modèle de données runtime du client (état du monde + du joueur).
// Réécriture C++ PROPRE (pas byte-exact) des tableaux d'entités et du « bloc self »
// relevés dans le désassemblage. Voir mémoires ts2-entity-model / ts2-gameplay-logic
// et Docs/TS2_GAMEPLAY_LOGIC.md. Les systèmes (StatEngine, Combat, Item, Skill,
// EntityManager…) opèrent sur ces structures ; les handlers réseau les peuplent.
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <string>

namespace ts2::game {

// Déclaration anticipée — `NpcDefRecord` (enregistrement de la table mNPC, 11736 o) est défini
// dans Game/ExtraDatabases.h, qui inclut DÉJÀ ce fichier (pour DataTable) : un include en sens
// inverse créerait un cycle. `NpcRenderEntry::def` (ci-dessous) n'étant qu'un POINTEUR, le type
// incomplet suffit ; les lecteurs qui déréférencent `def` (Scene/WorldRenderer.cpp) incluent
// déjà Game/StaticNpcLoader.h -> Game/ExtraDatabases.h.
struct NpcDefRecord;

// Identité réseau d'une entité : paire u32 (id_hi @payload+0, id_lo @payload+4).
struct EntityId {
    uint32_t hi = 0;  // netId1 (idHi) — ex-VeryOldClient: mServerIndex (avatar/item) / mIndex (monstre) [CONFIRMED, Rosetta §1]
    uint32_t lo = 0;  // netId2 (idLo) — ex-VeryOldClient: mUniqueNumber [CONFIRMED, Rosetta §1]
    bool valid() const { return hi != 0 || lo != 0; }
    bool operator==(const EntityId& o) const { return hi == o.hi && lo == o.lo; }
};

// Buff/debuff actif — modèle MINIMAL ajouté pour piloter UI/BuffStatusPanel (mission UI
// grille de buffs, 2026-07-14 ; voir Docs/TS2_UI_GAMEHUD_RENDER.md §9). AUCUN équivalent
// direct dans le layout stride-908 d'origine de dword_1687234 : le binaire ne stocke pas
// les buffs comme une liste sur l'entité joueur, il les RECALCULE chaque frame depuis
// ~50 variables globales de systèmes disjoints et non liés entre eux (combos élémentaires,
// synergies de paire, loadout de compétences, rang de guilde, gemme d'arme, harmonie
// élémentaire, debuffs à durée dword_16758D8, bonus serveur dword_1674AB0, buff nourriture
// morph, maîtrise élémentaire — cf. doc §9 points 1-14, aucun de ces systèmes n'est encore
// modélisé côté GameState). `ActiveBuff` est donc une ABSTRACTION DE COMMODITÉ pour la
// réécriture C++, pas un miroir d'offset : elle laisse un point d'ancrage unique où les
// futurs handlers réseau/systèmes de jeu pourront pousser un état de buff, sans bloquer le
// rendu du widget en attendant que ces ~50 sources soient reversées une à une.
struct ActiveBuff {
    int   id         = 0;    // identifiant catalogue (voir UI::BuffIconId dans
                              // UI/BuffStatusPanel.h — 0..33 retombent sur une icône .IMG
                              // réellement résolue par RE statique ; toute autre valeur (y
                              // compris les 36 emplacements de la banque de debuffs à durée
                              // §9.10, non résolue en fichier) retombe sur une pastille
                              // colorée générique)
    float expiryTime = 0.0f; // g_GameTimeSec d'expiration (0 = durée indéterminée/permanent)
};

// ---------------------------------------------------------------------------------------
// État runtime de la FSM d'anim/action (Char_UpdateAnimationFrame 0x571880) — MIROIR EN
// PRIMITIVES des champs consommés par Game/ActionStateMachine.h::ActionFsm et
// Game/AnimationTick.cpp (mission "SYSTEME ANIMATION/COLLISION", 2026-07-14). Primitives
// plutôt qu'un ActionFsm stocké PAR VALEUR ici, pour éviter un cycle d'include
// GameState.h <-> ActionStateMachine.h <-> CombatSystem.h (CombatSystem.h est marqué
// "DÉJÀ ÉCRIT, NE PAS ÉDITER" et inclut déjà GameState.h ; ActionStateMachine.h inclut
// CombatSystem.h — un include GameState.h -> ActionStateMachine.h créerait donc un cycle).
// Game/AnimationTick.cpp fait l'aller-retour CharAnimState <-> ActionFsm à chaque tick
// (construit un ActionFsm transitoire sur la pile, y copie ces champs, appelle sa logique
// déjà écrite, recopie le résultat ici). Offsets d'origine en commentaire (entity+N, cf.
// Game/ActionStateMachine.h pour la table complète des offsets déjà relevés).
// ---------------------------------------------------------------------------------------
struct FxTimerSlot {
    bool  active = false;
    float frame  = 0.0f;
};

struct CharAnimState {
    // --- Miroir direct de ActionFsm (état/timing/hit-detection) ---
    // Bloc MOVE-STATE 72 o @entity+240 (= body+216) : émis par Net_SendPacket_Op16 0x4B49F0,
    // recopié verbatim dans le body 600 o au spawn (Pkt_SpawnCharacter 0x4646C0). move-state+0 =
    // animSlot (sélecteur de CLIP d'anim = 2*weaponClass idle / +1 recovery). Passé comme `weaponType`
    // à PcModel_ResolveEquipSlot 0x4E46A0 @0x4e578e (base + 19968*animSlot + 156*actionState) ->
    // sélectionne le jeu MOTION de la POSE D'ARME. Sans lui (weaponType=0 figé), un joueur ARMÉ jouait
    // le clip DÉSARMÉ. DISTINCT de weaponAnimSlot (entity+220 = body+196 = traînée d'arme). // 0x574830
    int32_t animSlot          = 0;     // entity+240 = body+216 = move-state+0 (Char_TickMoveState arg4 @0x5748e0)
    int32_t state             = 0;     // entity+244 (CharActionState, miroir "facing") — ex-VeryOldClient: aType (ACTION_INFO ; nom aType/aSort permutable, Rosetta §7) [CONFIRMED offset]
    float   animFrame         = 0.0f;  // entity+248 — ex-VeryOldClient: aFrame [CONFIRMED, Rosetta §2]
    bool    hitCheckActive    = false; // entity+624 (idx156) — ex-VeryOldClient: mUsingSkill (nom tentatif) [CONFIRMED offset, Rosetta §3.5]
    bool    hitFired          = false; // entity+640 (idx160) — ex-VeryOldClient: mIsAttack [CONFIRMED offset, Rosetta §3.5]
    bool    hitUsesSkillTable = false; // entity+628 (idx157) — PLAUSIBLE (VeryOldClient: SGAttackState) — non prouvé IDA [Rosetta §3.5]
    bool    altWeaponSet      = false; // entity+576 (idx144)
    int32_t weaponAnimSlot    = 0;     // entity+220 (idx55)
    int32_t lastSkillEventId  = 0;
    int32_t actionKind        = 1;     // entity+632 (idx158) — PLAUSIBLE (VeryOldClient: BAttackState) — non prouvé IDA [Rosetta §3.5]
    int32_t actionSubKind     = 1;     // entity+636 (idx159) — PLAUSIBLE (VeryOldClient: RAttackState) — non prouvé IDA [Rosetta §3.5]
    int32_t guardSubstate     = 0;     // entity+552
    bool    guardKeyHeld      = false; // entity+548 (entrée, fournie par l'appelant)
    int32_t modelIndex        = 0;     // entity+92  (idx23) — ex-VeryOldClient: aTribe (RACE 0..2) [CONFIRMED, Rosetta §3.2]
    int32_t modelVariant      = 0;     // entity+96  (idx24) — ex-VeryOldClient: aGender (0..1) [CONFIRMED, Rosetta §3.2]
    int32_t weaponClass       = 0;     // Weapon_ClassFromField56 résolu en amont (hors périmètre)

    // --- Countdown UI de cast (dword_1675704/1675700 global, ce champ = this+16/+20) ---
    float cooldownA = 0.0f; // entity+16 (idx4) — décompte lié au global dword_1675704 — PLAUSIBLE (VeryOldClient: mUpdateTimeForRageTime) — non prouvé IDA [Rosetta §3.1]
    float cooldownB = 0.0f; // entity+20 (idx5) — décompte simple, inconditionnel — PLAUSIBLE (VeryOldClient: mUpdateTime3) — non prouvé IDA [Rosetta §3.1]

    // --- Latch générique 10s (entity+748/752) — sémantique exacte non déterminée ---
    bool  genericLatch10s      = false;
    float genericLatch10sStamp = 0.0f;

    // --- 8 timers FX secondaires (entity+820..877, cf. Char_UpdateAnimationFrame
    // 0x571DE4..0x572425) : indices 0..4 = tables "doubles" paramétrées par modelIndex/
    // modelVariant (choix selon weaponAnimSlot!=0 && !altWeaponSet), indices 5..7 = tables
    // "simples" fixes (pas de branche). Ordre = ordre d'apparition dans le binaire.
    std::array<FxTimerSlot, 8> fxTimers{};

    // --- Paire "==1" partagée (entity+888/892 et +896/900, même table unk_B68954) ---
    FxTimerSlot fx222{}; // idx222/223 (test ==1)
    FxTimerSlot fx224{}; // idx224/225 (test ==1)

    // --- Timer "boucle infinie" (entity+572/904) : remet frame à 0.0 SANS jamais clear
    // le flag (contrairement aux autres) — reproduit tel quel (0x5724CC..0x572512).
    int32_t fxLoopMode  = 0;    // entity+572 (idx143), testé ==1
    float   fxLoopFrame = 0.0f; // entity+904 (idx226)

    // --- Aura spéciale (entity+180/884) ---
    int32_t fxAuraTriggerField  = 0;     // entity+180 (idx45), testé ==2160
    bool    fxAuraAttachedLatch = false; // entity+884 (idx221)

    // --- Rotation faciale lissée (entity+276/280, 0x572531..0x572649) ---
    float facingCurrentDeg = 0.0f; // entity+276 (idx69, MUTÉ à 540°/s vers facingTargetDeg) — ex-VeryOldClient: aFront [CONFIRMED, Rosetta §2]
    float facingTargetDeg  = 0.0f; // entity+280 (idx70, lu seul) — ex-VeryOldClient: aTargetFront [CONFIRMED, Rosetta §2]

    // --- Marque de guilde en attente (entity+68) ---
    bool hasPendingGuildMark = false; // entity+68 (idx17), testé ==1 — ex-VeryOldClient: aGuildMarkEffect [CONFIRMED, Rosetta §3.2]
};

// Personnage/joueur distant — tableau dword_1687234 (stride 908, index 0 = self).
struct PlayerEntity {
    bool     active = false;              // +0 — ex-VeryOldClient: mCheckValidState [CONFIRMED, Rosetta §1/§3.1]
    EntityId id;                          // +4/+8 — ex-VeryOldClient: mServerIndex (idHi) / mUniqueNumber (idLo) [CONFIRMED, Rosetta §3.1]
    float    timestamp = 0.0f;            // +0x0C — ex-VeryOldClient: mUpdateTime [CONFIRMED, Rosetta §3.1]
    std::array<uint8_t, 600> body{};      // +0x18 apparence/équip/stats (payload 0x0F) — ex-VeryOldClient: aEquipForView (snapshot équip distants ; arme = body+148 = equipSnapshot+56, ex mEquip[7]) [CONFIRMED, Rosetta §3.3]
                                           // Layout partiellement reversé : voir
                                           // Docs/TS2_PLAYER_BODY_MODEL.md. Résolu avec
                                           // preuve de décompilation : body+148 (u32 LE)
                                           // = id ITEM_INFO de l'arme équipée (n'importe
                                           // quel joueur du tableau, cf. doc §2). Indice
                                           // convergent (pas une preuve directe) :
                                           // body+68/+72 = job(0..2)/faction(0..1)
                                           // probables (doc §3bis). Reste NON résolu :
                                           // casque/armure/bottes/accessoires (12 des 13
                                           // slots d'équip probables, doc §3) et le
                                           // point d'attache réel du rendu du corps
                                           // équipé en jeu (doc TS2_ENTITY_ARRAY_
                                           // DUALITY_CHECK.md §3 — non localisé
                                           // statiquement).
    float    x = 0.0f, y = 0.0f, z = 0.0f;// bloc pos @+0xF0 — ex-VeryOldClient: aLocation (ACTION_INFO, move-state+12) [CONFIRMED, Rosetta §2]
    // Orientation (cap horizontal, degrés) — mission ROTATION/ORIENTATION, 2026-07-14.
    // body+252 = move-state[216]+36 (bloc move-state = {moveVal@+0, actionState@+4,
    // animFrame@+8, posX@+12, posY@+16, posZ@+20, ..., heading@+36}, cf. bandeau de tête
    // de Game/EntityManager.cpp). CONFIRMÉ par DEUX décompilations indépendantes qui
    // convergent bit-à-bit sur le même offset relatif :
    //   1) CharAnimState::facingCurrentDeg (ci-dessous, entity+276 = body+252 puisque le
    //      body démarre à entity+0x18) — Char_UpdateAnimationFrame 0x571880 MUTE ce champ
    //      à 540°/s vers facingCurrentDeg->facingTargetDeg (entity+280 = body+256), avec
    //      wraparound 360° — cf. Game/AnimationTick.cpp.
    //   2) Char_Draw 0x5805C0 (décompilation directe idaTs2, cette mission) : pour le
    //      MONSTRE (this = &dword_1766F74[i] DIRECTEMENT, aucun tableau intermédiaire —
    //      cf. Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §1), le champ `*((float*)this+14)`
    //      (= record+56 = body+40 = move-state[4]+36, MÊME décalage relatif que le joueur
    //      en tenant compte du move-state qui démarre à body+4 au lieu de body+216) est
    //      injecté comme composante Y d'un vecteur de rotation {0, heading, 0} passé à
    //      SObject_DrawEx -> Model_Render 0x40EBB0 (rôle IDB confirmé : « compose la
    //      matrice monde S*Rz*Ry*Rx*T ») ; le vecteur d'échelle correspondant est câblé en
    //      DUR à {1,1,1} dans SObject_DrawEx — donc CE champ est bien une rotation, jamais
    //      une échelle. ÉCART DE FIDÉLITÉ CORRIGÉ par cette mission : l'ancien commentaire
    //      "this+7=angle, this+8=pos, this+14=scale" de Scene/WorldRenderer.h/
    //      Game/EntityDrawLogic.h (écrit sans accès à Model_Render, serveur MCP saturé à
    //      l'époque) est ERRONÉ — this+7 = animFrame (move-state+8), this+14 = heading
    //      (move-state+36). Non modifié dans EntityDrawLogic.h (juste annoté) pour ne pas
    //      perturber ComputeBodyMeshPlacement, qui reste fonctionnellement correct : c'est
    //      la COUCHE APPELANTE (WorldRenderer.cpp) qui doit alimenter facingOrAnimTimer
    //      avec CE champ `heading`, pas un octet brut d'un objet de rendu séparé.
    // Miroir top-level (même convention que x/y/z ci-dessus) : peuplé par
    // EntityManager::ReadPlayerPos-adjacent (cf. Game/EntityManager.cpp), lu directement
    // par Scene/WorldRenderer.cpp pour la rotation Y de la matrice monde par entité.
    // NE DUPLIQUE PAS anim.facingCurrentDeg (CharAnimState, non muté ici) : ce miroir est
    // la valeur BRUTE la plus récente reçue du réseau (snap immédiat), le lissage 540°/s
    // de CharAnimState restant un système séparé, non câblé sur les entités distantes à
    // ce jour (cf. Game/AnimationTick.h, Char_UpdateAnimationFrame jamais appelée depuis
    // EntityManager/WorldRenderer).
    float    heading = 0.0f;              // body+252 = move-state+36 — ex-VeryOldClient: aFront [CONFIRMED, Rosetta §2]
    int      hp = 0, mp = 0;              // +7208 (self.hp dans cGameData)

    // Nom du personnage (mission NAMEPLATES, 2026-07-14) : entity+72 raw, soit
    // body+48 (72-24, cf. convention body@+0x18 ci-dessus) = payload Pkt_SpawnCharacter
    // +56 (8 + 48, le body réseau démarrant à l'offset payload 8). Confirmé par
    // décompilation Hex-Rays de Char_DrawNameplate 0x56EF40 : `Crt_Vsnprintf(v115, "%s",
    // this + 72)` — lecture directe en C-string, AUCUNE autre étape de résolution (pas
    // une table externe). Extrait par EntityManager::ReadPlayerName (Game/EntityManager.cpp)
    // à chaque Pkt_SpawnCharacter (spawn ET update, le body étant recopié en entier).
    // Longueur du buffer NON confirmée avec certitude à l'octet près (aucun struct nommé
    // dans l'IDB à cet offset) : le prochain champ lu par 0x56EF40 après le nom est
    // `element` à entity+88 (body+64), ce qui borne le buffer à 16 o maximum (72..87) —
    // valeur reprise ici comme longueur max de lecture (cf. kPNameBufLen). Cohérent avec
    // les buffers `char[13]` (12 caractères + NUL) vus ailleurs dans le protocole
    // (Docs/TS2_PROTOCOL_SPEC.md, ex. friendName/target_name) : 16 o laisse de la marge
    // pour l'alignement sans tronquer un nom légitime.
    std::string name;                     // entity+72 (body+48) — ex-VeryOldClient: aName [CONFLICT §7 C1 résolu : décl. struct IDA disait name[48]@40, décompil. Char_DrawNameplate 0x56EF40 (this+72) prouve @+72 → IDA gagne]

    // État minimal ajouté (mission UI buffs, 2026-07-14) : buffs/debuffs actifs du JOUEUR
    // LOCAL (index 0 du tableau = self, cf. commentaire ci-dessus), consommé exclusivement
    // par UI/BuffStatusPanel::RenderGrid(). Vide par défaut → le widget dessine une grille
    // vide (ne bloque jamais le rendu) tant qu'aucun système amont ne l'alimente.
    std::vector<ActiveBuff> buffs;

    CharAnimState anim{}; // FSM d'anim/action (Char_UpdateAnimationFrame), cf. ci-dessus
};

// Monstre — tableau dword_1766F74 (stride 280).
struct MonsterEntity {
    bool     active = false;              // +0 — ex-VeryOldClient: mCheckValidState [CONFIRMED, Rosetta §1/§4]
    EntityId id;                          // +4/+8 — ex-VeryOldClient: mIndex (idHi) / mUniqueNumber (idLo) [CONFIRMED, Rosetta §4]
    float    timestamp = 0.0f;            // +0x0C — ex-VeryOldClient: mUpdateTime [CONFIRMED, Rosetta §4]
    std::array<uint8_t, 80> body{};       // body[0] = mob id -> MONSTER_INFO ; +0x10 — ex-VeryOldClient: mDATA (OBJECT_FOR_MONSTER) [CONFIRMED, Rosetta §4]
    const void* def = nullptr;            // +0x60 record MONSTER_INFO résolu — ex-VeryOldClient: mMONSTER_INFO [CONFIRMED, Rosetta §4]
    float    radius = 0.0f;               // +0x64 — ex-VeryOldClient: mRadiusForSize [CONFIRMED, Rosetta §4]
    int      hp = 0;                      // +923784 (dans cGameData)
    float    x = 0.0f, y = 0.0f, z = 0.0f;// record+32/36/40 (body+16/20/24) — ex-VeryOldClient: mAction.aLocation [CONFIRMED, Rosetta §4]
    // Orientation (cap horizontal, degrés) — body+40 = move-state[4]+36 (même layout
    // move-state que PlayerEntity, décalé : démarre body+4 au lieu de body+216 — cf.
    // bandeau de tête de Game/EntityManager.cpp). Preuve DIRECTE (pas seulement par
    // analogie avec le joueur) : Char_Draw 0x5805C0 (décompilation idaTs2, mission
    // ROTATION/ORIENTATION 2026-07-14) opère avec `this = &dword_1766F74[i]` — CE
    // TABLEAU, confirmé sans tableau de rendu intermédiaire par
    // Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §1 (xrefs_to + désassemblage des 2 sites
    // d'appel dans Scene_InGameRender). Le champ `*((float*)this+14)` (= record+56 =
    // body+40) est injecté comme composante Y du vecteur de rotation {0,heading,0}
    // passé à SObject_DrawEx -> Model_Render 0x40EBB0 (« compose la matrice monde
    // S*Rz*Ry*Rx*T », rôle IDB) ; le vecteur d'échelle correspondant est câblé en DUR à
    // {1,1,1} dans SObject_DrawEx — ce champ est donc une rotation, jamais une échelle
    // (cf. commentaire détaillé sur PlayerEntity::heading ci-dessus pour l'écart de
    // fidélité corrigé vis-à-vis de l'ancien commentaire WorldRenderer.h/
    // EntityDrawLogic.h "this+14=scale"). Peuplé par EntityManager (miroir top-level,
    // même convention que x/y/z), consommé par Scene/WorldRenderer.cpp.
    float    heading = 0.0f;              // record+56 (body+40 = move-state[4]+36) — ex-VeryOldClient: mAction.aFront [CONFIRMED, Rosetta §4]
    CharAnimState anim{}; // FSM d'anim/action (Char_UpdateAnimationFrame), cf. ci-dessus
};

// NPC — tableau dword_17AB534 (stride 152).
struct NpcEntity {
    bool     active = false;              // +0 — ex-VeryOldClient: mCheckValidState [CONFIRMED, Rosetta §5 ; ⚠️ VeryOld le place @+4 (layout NPC_OBJECT divergent), seul le RÔLE est aligné, cf. §7 C3]
    EntityId id;                          // +4/+8 (idHi/idLo) — absent du wrapper VeryOld réduit → IDA seul [CONFIRMED, Rosetta §5]
    float    timestamp = 0.0f;            // +0x0C — absent VeryOld → IDA seul [CONFIRMED, Rosetta §5]
    std::array<uint8_t, 84> body{};       // body[0] = mob id -> NPC/MobDb ; +0x10 — PLAUSIBLE (VeryOldClient: nAction) — non prouvé IDA [Rosetta §5]
    const void* def = nullptr;            // +0x64 — CONFLICT §7 C2 : VeryOld nInfo typé NPC_INFO* @+0, IDA prouve ITEM_INFO* @+100 (MobDb_GetEntry 0x4C3C00) → IDA gagne (type + offset)
    uint32_t action = 0;
    // Position monde @body+16/20/24 (confirmé par décompilation Hex-Rays de
    // Char_SelectAuraEffect 0x5835B0, appelé juste après la copie du body dans
    // Pkt_SpawnNpc 0x467EC0 : this+8/9/10 == record+32/36/40 == body+16/20/24,
    // même convention que MonsterEntity::x/y/z (body+16/20/24). Déjà reçue dans
    // `body` via le réseau (Pkt_SpawnNpc), juste jamais extraite avant ce jalon.
    float    x = 0.0f, y = 0.0f, z = 0.0f;// record+32/36/40 (body+16/20/24) — ex-VeryOldClient: nAction.aLocation [CONFIRMED, Rosetta §5]
    // PAS de champ `heading` ici (mission ROTATION/ORIENTATION, 2026-07-14) :
    // contrairement à PlayerEntity/MonsterEntity (cf. leurs commentaires `heading`),
    // AUCUNE fonction du call-graph de rendu n'appelle Char_Draw/Char_DrawReflection
    // sur `dword_17AB534` (les PNJ sont dessinés via `Npc_DrawMesh 0x57FF00` sur un
    // tableau de RENDU séparé `g_NpcRenderArray` 0x1764D14, cf.
    // Docs/TS2_NPC_MESH_DRAW.md et Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §2) — le
    // champ heading éventuel de `Npc_DrawMesh` vivrait sur CET AUTRE tableau, non
    // adossé à `NpcEntity`/`g_World.npcs` (même lacune déjà documentée pour le corps
    // PNJ, cf. Scene/WorldRenderer.h). Ajouter un `heading` ici serait une INVENTION
    // (aucun offset confirmé sur ce record précis) ; `StaticNpcLoader.h::angle` est un
    // système NON LIÉ (PNJ de décor statiques chargés par zone via
    // cGameData_LoadZoneNpcInfo, pas les PNJ réseau de ce tableau).
    // PLAUSIBLE (VeryOldClient: NPC_OBJECT.nFront = cap NPC) — non prouvé IDA : aucun
    // offset heading confirmé sur ce record 152 o (§8 Rosetta, non ancré → non ajouté).
};

// ---------------------------------------------------------------------------------------
// PNJ — entrée du pool de RENDU/CIBLAGE `g_NpcRenderArray` (dword_1764D14, stride 88 o /
// 22 dw, capacité FIXE 100 = g_NpcCount 0x1687220).
//
// ⚠️ CE POOL N'A JAMAIS PORTÉ D'OBJETS AU SOL. Il s'appelait `GroundItem` ici jusqu'à la
// Passe 4 / vague W7 (front « npc-array-unify ») : ce nom était ERRONÉ, et le pool était
// modélisé DEUX FOIS côté C++ — ce champ (`GameWorld::groundItems`, JAMAIS peuplé, donc tous
// ses consommateurs étaient morts) et le `std::vector` privé de Game/StaticNpcLoader.cpp (seul
// peuplé, mais ignoré du clic/tick/ciblage). Les deux représentations sont désormais FUSIONNÉES
// ici : `GameWorld::npcRenderEntries` est le pool unique, StaticNpcLoader::LoadZoneNpcs() y
// écrit directement, et `game::ZoneNpcs()` n'en est plus qu'un accesseur.
//
// ÉCRIVAIN UNIQUE — cGameData_LoadZoneNpcInfo 0x5578E0 (re-prouvé par décompilation, W7).
// `this` = g_LocalPlayerSheet 0x1685748 (`mov ecx, offset g_LocalPlayerSheet` @0x4648F7,
// juste avant le `call` @0x4648FC) ; `this` est typé `float*` et l'immédiat 228723 dw vaut
// 914892 o = 0xDF5CC, or 0x1685748 + 0xDF5CC = 0x1764D14 EXACTEMENT. Le pool n'apparaît donc
// PAS dans les xrefs de 0x1764D14 (adressage `this + offset immédiat`) : preuve par calcul
// d'offset, pas par xref.
//
// Champs PROUVÉS (les SEULS écrits/lus des 88 o — le reste (+8, +32..43, +48..79, +84..87)
// n'est touché par aucun écrivain/lecteur connu et n'est donc PAS modélisé, cf. règle
// « régions non prouvées ») :
//   +0  def       = SkillDefTbl_GetRecord(mNPC, kindId)          @0x557946
//   +4  active    = 1 (flag occupé)                              @0x55796B
//   +12 mode      = 0                                            @0x55797F
//   +16 frameAcc  = 0.0                                          @0x557995
//   +20/24/28 x/y/z ← mZONENPCINFO 0x14AA930 +0x194   @0x5579C1/0x5579ED/0x557A19
//   +44 angle       ← mZONENPCINFO +0x644                        @0x557A42
//   +80 angleBase = *(+44)                                       @0x557A62
//
// LECTEURS — TOUS traitent ce pool en PNJ, jamais en objet au sol : Npc_DrawMesh 0x57FF00,
// Npc_RenderSlotTick 0x5803A0 (+ _Loop 0x580400 / _Once 0x5804A0), Scene_RayHitNpcBox
// 0x541680, World_PickEntityAtCursor 0x538AB0 (boucle j, borne g_NpcCount, catégorie de clic
// 4 -> Npc_ApproachAndInteract @0x53723F), UI_NpcWin_Open 0x5DB530. Les sacs de butin vivent
// AILLEURS : dword_17AB534 (stride 152, catégorie de clic 6 -> Npc_Interact @0x536AB9), cf.
// `GameWorld::npcs`.
// ---------------------------------------------------------------------------------------
struct NpcRenderEntry {
    // +0 — enregistrement mNPC résolu (11736 o, cf. Game/ExtraDatabases.h). Écrit
    // INCONDITIONNELLEMENT (@0x557946), y compris à nullptr si la résolution échoue : c'est
    // la garde qui suit (@0x557956) qui décide de l'activation.
    const NpcDefRecord* def = nullptr;
    // +4 — flag « slot occupé » (DWORD 0/1 d'origine). SEUL champ remis à 0 par le
    // constructeur de slot maybe_cGameData_ListField1Reset 0x57FE50 (`*(this+1) = 0`, appelé
    // 100x par cGameData_InitPools 0x5575D0) ET par le destructeur 0x57FE70 (`*(this+1) = 0`,
    // bouclé par Pkt_EnterWorld @0x464237) : désactiver un slot NE remet AUCUN autre champ à
    // zéro — les valeurs résiduelles (def/pos/angle) sont conservées telles quelles.
    bool active = false;
    // +12 — sélecteur de mode de tick, lu par Npc_RenderSlotTick @0x5803BA : 0 -> _Loop
    // (0x580400), 1 -> _Once (0x5804A0), TOUTE AUTRE valeur -> no-op. Le MÊME champ sert
    // d'animId dans _Loop (`Model_GetWeaponEffectFrameCount(g_ModelMotionArray, def+1324 - 1,
    // *(this+3))` @0x580429) : double rôle porté tel quel par le binaire.
    int mode = 0;
    // +16 — accumulateur de frame : `+= dt*30.0` puis wrap sur frameCount
    // (@0x58043E / @0x58045F).
    float frameAcc = 0.0f;
    // +20/24/28 — position monde. Vec3 CONTIGU d'origine : Math_Dist3D((float*)this + 5,
    // flt_1687330) @0x580483 le lit comme un vec3 à partir de +20.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    // +44 — angle affiché courant (rotation Y du maillage, Npc_DrawMesh 0x57FF00).
    float angle = 0.0f;
    // +80 — angle « baseline » : recopié DANS +44 par _Loop dès que la distance au joueur
    // local dépasse 400.0 (`*(this+11) = *(this+20)` @0x58048E).
    float angleBase = 0.0f;
};

// Capacité FIXE du pool — cGameData_InitPools 0x5575D0 @0x5575E9 (`*((_DWORD*)this + 1718) =
// 100` -> 0x1685748 + 1718*4 = 0x1687220 == g_NpcCount). Ce n'est PAS un compteur d'occupation
// (jamais réécrit par le chargeur) mais une CAPACITÉ, qui sert aussi de BORNE aux lecteurs
// (World_PickEntityAtCursor boucle `j < g_NpcCount`). L'occupation réelle est portée par le
// seul flag `active` (+4). Cohérent avec mZONENPCINFO, qui porte structurellement 100 entrées
// par zone (row = 501 dw = 4 count + 100 kindId + 300 pos + 100 angle = 2004 o).
inline constexpr int kNpcRenderPoolCapacity = 100;

// Alias TRANSITOIRE de l'ancien nom erroné (cf. bandeau ci-dessus), conservé UNIQUEMENT le
// temps que les lecteurs hors périmètre de la vague W7 soient renommés (Game/
// GroundAuraWorldObjectTick.*, Game/ItemPickupSystem.cpp, Game/AutoTargetCombatGate.*).
// NE PAS UTILISER DANS DU CODE NEUF : ce pool ne contient pas d'objets au sol.
using GroundItem = NpcRenderEntry;

// Objet de zone / nœud de ressource (mine, portail, etc.) — tableau
// dword_180EEF4 (stride 76 o / 19 dw), compteur dword_1687230, capacité fixe
// 500 (cf. cGameData_InitPools 0x5575D0, pool F). Peuplé par Pkt_SpawnZoneObject
// (opcode 0x86) ; layout confirmé par Docs/TS2_PROTOCOL_SPEC.md ([SC b08]) :
// +0x00 = actif, +0x04/+0x08 = paire d'identifiants objet, +0x0C = horodatage
// de spawn (flt_815180), +0x18..+0x4C = bloc 52 o non décodé plus finement à
// ce jour. Pool DISTINCT du pool de projectiles (dword_17D06F4/g_FxAuraCount,
// cf. Docs/TS2_FX_CATALOG.md) malgré des compteurs voisins en mémoire
// (0x168722C puis 0x1687230, juxtaposés dans le bloc cGameData).
struct ZoneObjectEntity {
    bool     active = false;              // +0x00
    uint32_t objId1 = 0;                  // +0x04
    uint32_t objId2 = 0;                  // +0x08
    float    spawnTimestamp = 0.0f;       // +0x0C
    std::array<uint8_t, 52> body{};       // +0x18 (52 o, non décodé)
};

// Cellule d'inventaire (6 dwords : {itemId, gridX, gridY, flag, color/appearance, durability}).
// PAS de champ `page` ici : la dimension "page/row" du tableau plat g_InvMain
// [384*row + 6*col] du binaire (Pkt_ItemUpgradeResult 0x488DE0, Pkt_ItemActionDispatch
// 0x46A320, cGameHud_InvCellAt 0x64F9F0) est portée par l'INDEX (row, col) dans
// game::ClientRuntime::InventoryState::cells (Game/ClientRuntime.h), pas par une
// donnée de la cellule elle-même — un `page` stocké DANS InvCell serait une 3e
// représentation de la même dimension, redondante avec l'index du tableau qui la
// porte déjà (cf. réconciliation des modèles d'inventaire, mission « inventaire »,
// 2026-07-14). row 0 = sac principal (toujours actif), row 1 = page bonus, visible
// dans l'UI seulement si g_Client.VarGet(0x16732A8) >= 1 (g_Inv_ExtraPageCount, cf.
// UI/InventoryWindow.h).
struct InvCell {
    uint32_t itemId = 0, gridX = 0, gridY = 0, flag = 0, color = 0, durability = 0;
    bool empty() const { return itemId == 0; }
};

// Un slot d'équipement (13 slots, stride 16 o d'origine).
struct EquipSlot {
    uint32_t itemId = 0;
    uint32_t socket = 0;   // mot bit-packé (grade/gemme/enchant)
    uint32_t extra0 = 0, extra1 = 0;
};

// État du joueur local — le « self work block » (globals 0x16731A8.. de l'original).
struct SelfState {
    int level          = 1;   // g_SelfLevel 0x16731A8 — ex-VeryOldClient: OBJECT_FOR_AVATAR.aLevel1 [CONFIRMED, Rosetta §6]
    int levelBonus     = 0;   //             0x16731AC — ex-VeryOldClient: aLevel2 [CONFIRMED, Rosetta §6]
    // 4 attributs primaires (les suffixes = offsets ITEM_INFO correspondants).
    int attrExtForce   = 0;   // g_SelfBaseAttr292 0x16731BC
    int attrIntForce   = 0;   //               296 0x16731C4
    int attrDefensive  = 0;   //               300 0x16731B8
    int attrOffensive  = 0;   //               304 0x16731C0
    int unspentAttr    = 0;   // 0x16731D0
    int skillPoints    = 0;   // 0x16731D4
    int growthIndex    = 0;   // g_GrowthIndex 0x1674774 (= tier*100 + palier 1..15)
    int currency       = 0;   // g_Currency 0x1673180
    // Nombre de pages BONUS d'inventaire débloquées (g_Inv_ExtraPageCount 0x16732A8,
    // écrit par Pkt_SetGameVar case 88, cf. Net/GameVarDispatch.cpp) : PAS de champ
    // dédié ici (un second champ non écrit par le même handler serait une nouvelle
    // duplication silencieuse — c'est justement ce que cette mission élimine). Lire
    // game::g_Client.VarGet(0x16732A8) directement (déjà fait par Game/QuestSystem.cpp
    // et par UI/InventoryWindow.h::SetBagPage). 0 = seule la page 0 (8x8=64 cases) est
    // accessible ; >=1 débloque la page 1 (cGameHud_OnMouseDown case 1, bouton
    // unk_93F88C ; sinon message StrTable005_Get(156) "pas de place"). Le binaire
    // n'expose jamais plus de 2 pages au total (cf. UI/InventoryWindow.h::kMaxBagPages).
    int element        = 0;   // g_LocalElement 0x1673194
    int elementSecondary = 0; //                0x1673198
    // Mode de combat/action du self (self.mode, cGameData+7136) : lu par les reflexes
    // reseau self-mode (Net/CombatResultApply.cpp, Passe 3 W2-F5 ; le vrai jeu declenche
    // les reactions quand mode in {1,5,6,7}). 0 = mode normal. Champ ajoute par MAIN (etat
    // partage de la vague W2) pour eviter toute double ecriture inter-front sur GameState.h.
    int mode = 0;
    uint32_t weaponId  = 0;   // g_LocalPlayerWeaponId 0x16731E8 — ex-VeryOldClient: mEquip[7] (arme ; = equip[7].itemId, alias dword_1673248) [CONFIRMED, Rosetta §6]

    // Nom du joueur local (13 o NUL-terminé côté binaire, probablement byte_1673184 —
    // adresse aussi référencée ailleurs sous d'autres sémantiques disputées, cf. Game/
    // SkillCombat.h ; NE PAS supposer laquelle des deux missions a raison, ce champ existe
    // ici indépendamment de cette adresse précise). Nécessaire pour les comparaisons
    // "suis-je le leader / suis-je la cible du départ" du roster d'alliance
    // (Net_OnGuildMemberLeave/Kick 0x4914D0/0x4916D0, Net_OnGuildRosterUpdate cas 3 — cf.
    // Docs/TS2_ALLIANCE_PARTY_ROSTER.md §3). AUCUN handler de ce module ne le peuple encore
    // (le nom local arrive normalement via le paquet d'entrée en jeu / login, hors périmètre
    // de la mission roster alliance) : reste vide par défaut -> toute comparaison
    // "== localPlayerName" échoue proprement (jamais de faux "c'est moi"), même politique de
    // dégradation honnête que UI/SocialWindow.cpp pour les champs non alimentés.
    std::string localPlayerName;

    // Position locale de spawn capturée depuis le slot CharSelect choisi (dword_166A8E0/
    // E4/E8[2522*slot], EA 0x51c79e-0x51c7d4 de Scene_CharSelectUpdate — mêmes offsets que
    // Net/CharSelectPackets.h::kCharRecFieldPosX/Y/Z). Consommée UNE SEULE FOIS par le
    // payload tail72 (72 o) de Net_SendPacket_Op12 0x4B43C0 envoyé depuis Scene_EnterWorldUpdate
    // case 2 (cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md) — écrasée ensuite par la position réelle
    // reçue du serveur au premier spawn, donc pas de conflit avec l'usage normal en jeu.
    // Mirroir écrit par UI/LoginScene.cpp::CharSelectUpdate() au même point que zoneId
    // ci-dessus (charState_.enterWorldZoneId).
    // RE-CONFIRMÉ (2026-07-14, décompilation fraîche) : dans le struct72 sérialisé, ces 3
    // floats vont aux offsets +0x0C/+0x10/+0x14 (flt_1675AAC/B0/B4), PAS +0x00/+0x04/+0x08 —
    // voir Net/CharSelectPackets.h::kTail72Off* pour le layout complet confirmé.
    float spawnX = 0.0f, spawnY = 0.0f, spawnZ = 0.0f;

    // Angle de spawn (degrés, 0..359), tiré par le CLIENT au même point que spawnX/Y/Z
    // ci-dessus (flt_1675AC4 = Rng_Next() % 360, EA 0x51c7ed de Scene_CharSelectUpdate,
    // dupliqué tel quel dans flt_1675AC8 juste après, EA 0x51c7f9 — PAS 2 angles distincts,
    // la même valeur est écrite 2 fois dans le struct72, cf. kTail72OffRotA/RotB). Le
    // serveur ne validant pas ce tirage, seule la FORMULE (Rng_Next()%360, même PRNG que
    // Net/Rng.h::Rng) compte pour la fidélité, pas la valeur elle-même. Peuplé par
    // UI/LoginScene.cpp::CharSelectUpdate() au même point que spawnX/Y/Z/zoneId ci-dessus.
    float spawnRotationDeg = 0.0f;

    std::array<EquipSlot, 13> equip{};   // g_EquipMain 0x16731D8 — ex-VeryOldClient: mEquip[MAX_EQUIP_SLOT_NUM] (offsets internes NON transposés : VeryOld stocke ITEM_INFO* par slot, IDA {itemId,socket,extra0,extra1} 16 o) [CONFIRMED, Rosetta §6]

    // Stats dérivées (calculées par StatEngine à partir de tout ce qui précède).
    int maxHp = 0, maxMp = 0, hp = 0, mp = 0;
    int extAtk = 0, intAtk = 0, extDef = 0, intDef = 0;
    int accuracy = 0, evasion = 0, critRate = 0;
    int atkRatingMin = 0, atkRatingMax = 0, attackSpeed = 0;

    // Grille d'inventaire : PAS ICI. Ancien modèle simplifié (vector<InvCell>,
    // coordonnées x/y libres) supprimé lors de la réconciliation des deux modèles
    // concurrents (mission « inventaire », 2026-07-14) : il divergeait en silence
    // de game::g_Client.inv (InventoryState, Game/ClientRuntime.h), le SEUL modèle
    // écrit par les handlers réseau déjà câblés (Net/GameHandlers_InvCells.cpp,
    // Net/ItemActionDispatch.cpp, etc.). Confirmé bit-exact par désassemblage :
    // g_InvMain/g_InvGrid_GridX/GridY/Count/Durability/InstanceSerial, adressage
    // [384*ligne + 6*colonne] (Pkt_ItemUpgradeResult 0x488DE0) et, de façon
    // indépendante, [(ligne%100)*0x600 + (colonne%100)*0x18] (Pkt_ItemActionDispatch
    // 0x46A320) — les deux réduisent à la même InventoryState::At(row,col) (kCols=64,
    // stride 6 dwords/cellule). Consommateurs (UI/InventoryWindow.cpp, HUD, etc.)
    // doivent lire/écrire game::g_Client.inv, PAS un champ ici.

    // Blocs bruts reçus par Pkt_EnterWorld (à décoder progressivement).
    std::vector<uint8_t> charInvBlock; // g_SelfCharInvBlock (10088 o, payload 0x0C) — PLAUSIBLE (VeryOldClient: MYINFO.mUseAvatar / AVATAR_INFO) — non prouvé IDA (VA non ancrée, §8 Rosetta)
    std::vector<uint8_t> zoneState;    // dword_16758D8 (288 o)
};

// Vue sur une table de données .IMG chargée (records à stride fixe).
struct DataTable {
    std::vector<uint8_t> data; // enregistrements bruts (déjà déchiffrés/décompressés)
    uint32_t count  = 0;
    uint32_t stride = 0;
    const uint8_t* record(uint32_t i) const {
        return (i < count) ? data.data() + static_cast<size_t>(i) * stride : nullptr;
    }
};

// Bases de données de jeu (fichiers .IMG 005_* — cf. Docs/TS2_IMG_FORMAT.md).
struct GameDatabases {
    DataTable level;   // LEVEL_INFO   (44 o)
    DataTable item;    // ITEM_INFO    (436 o)
    DataTable skill;   // SKILL_INFO   (776 o)
    DataTable monster; // MONSTER_INFO (944 o)
    DataTable socketT; // SOCKET_INFO  (20 o)
};

// Roster de GROUPE (noms) — miroir C++ de g_PartyRosterNames (0x1674608, 10 slots x 13 o,
// nom joueur uniquement). Peuplé par Net_OnPartyMemberNameSet (opcode SC 0x3e / EA 0x4909A0,
// écrit names[slotIndex]) et vidé par Net_OnPartyMemberClear (opcode SC 0x40 / EA 0x490AB0),
// cf. Net/GameHandlers_PartyGuild.cpp et Docs/TS2_ALLIANCE_PARTY_ROSTER.md §2 (mission
// "CABLAGE ROSTER GROUPE", 2026-07-14). `slotIndex` est un index de roster ASSIGNÉ PAR LE
// SERVEUR (0..9) — RIEN dans le désassemblage ne prouve qu'il correspond à l'index d'entité
// de GameWorld::players (dword_1687234, qui liste TOUTE entité joueur visible à proximité,
// alimenté par un mécanisme totalement différent : résolution par identité réseau via
// PartyMemberHpSet/PartyMemberUpdate). Les deux tableaux sont donc INDÉPENDANTS ; UI/
// PartyWindow.cpp les recoupe en best-effort par même indice faute de clé de jointure connue
// (voir commentaire dédié dans ce fichier).
struct PartyRoster {
    std::array<std::string, 10> names;
};

// Roster de GUILDE/ALLIANCE (noms) — miroir C++ de g_AllianceRosterNames (0x16749B8,
// 5 slots x 13 o : slot 0 = leader/fondateur, slots 1..4 = membres) + g_LocalGuildName
// (0x168740C, chaîne SÉPARÉE du tableau des 5 membres = nom de MA guilde/alliance active).
// Cf. Docs/TS2_ALLIANCE_PARTY_ROSTER.md §3 (mission "CABLAGE ROSTER ALLIANCE/GUILDE",
// 2026-07-14) pour le désassemblage complet des 6 handlers qui le peuplent :
//   Net_OnGuildRosterReset  (SC 0x4a / EA 0x4911D0) : reset intégral (5 noms + guildName).
//   Net_OnGuildMemberJoin   (SC 0x4b / EA 0x491330) : insère dans le 1er slot libre 1..4.
//   Net_OnGuildMemberLeave  (SC 0x4d / EA 0x4914D0) : self -> reset ; sinon retire+compacte.
//   Net_OnGuildMemberKick   (SC 0x4e / EA 0x4916D0) : idem Leave.
//   Net_OnGuildRosterUpdate (SC 0x4f / EA 0x4918D0) : cas 1/3 -> reset intégral (message
//     différent si slot 0 == nom local, i.e. JE suis le leader qui dissout) ; cas 2 -> aucune
//     mutation (message informatif seul).
// AVANT cette mission, `GameWorld::group` (type `GroupIdentity`, désormais RETIRÉ) mappait
// `guildName` sur unk_167468A/`groupName` sur unk_1674697 — c'était FAUX : ces deux adresses
// sont en réalité g_PendingReqTargetName_Sub2/Sub1 (buffer de nom de CIBLE d'une requête de
// confirmation en cours, guilde/faction — invitation/kick), PAS un nom de guilde/groupe actif.
// Verdict tranché et prouvé par désassemblage croisé dans Docs/TS2_ALLIANCE_PARTY_ROSTER.md
// §1 : les deux lectures (Scene_InGameUpdate ET Net/GameHandlers_ChatSocial.cpp) portent sur
// LA MÊME paire de buffers de requête, pas sur deux champs différents. Le vrai « nom de ma
// guilde active » est `AllianceRoster::guildName` ci-dessous (g_LocalGuildName 0x168740C).
struct AllianceRoster {
    static constexpr int kMaxSlots = 5; // slot 0 = leader/fondateur, 1..4 = membres

    std::array<std::string, kMaxSlots> memberNames{}; // g_AllianceRosterNames + 13*i
    std::string                        guildName;      // g_LocalGuildName 0x168740C

    bool Empty(int slot) const {
        return slot < 0 || slot >= kMaxSlots || memberNames[static_cast<size_t>(slot)].empty();
    }

    // Vrai si `name` (non vide) == memberNames[0] — slot 0 = leader/fondateur (cf. §3 : "le
    // slot 0 est traité spécialement — comparé au nom du joueur local dans plusieurs
    // handlers — cohérent avec slot 0 = leader/fondateur de l'alliance").
    bool IsLeader(const std::string& name) const {
        return !name.empty() && memberNames[0] == name;
    }

    // Net_OnGuildMemberJoin (0x491330) : boucle i=1;i<5, premier slot vide. Slot 0 (leader)
    // JAMAIS réattribué par ce chemin. Retourne false si les slots 1..4 sont tous occupés
    // (roster plein — comportement d'origine non spécifié dans ce cas, aucune mutation ici).
    bool AddMember(const std::string& name) {
        for (int i = 1; i < kMaxSlots; ++i) {
            if (memberNames[static_cast<size_t>(i)].empty()) {
                memberNames[static_cast<size_t>(i)] = name;
                return true;
            }
        }
        return false;
    }

    // Retire `name` du roster (recherche 0..4, cf. TODO d'origine "retirer nm de
    // g_AllianceRosterNames[0..5]") : si trouvé au slot 0 (le leader lui-même, cas limite non
    // attendu via ce chemin — le départ du leader passe normalement par la dissolution
    // complète de GuildRosterUpdate cas 1/3), reset intégral par prudence ; sinon compacte les
    // slots 1..4 (décale tout ce qui suit d'un cran, slot 0 jamais décalé). Algorithme de
    // compactage NON désassemblé au détail près dans cette mission (§3 : "non décompilées en
    // détail") — reproduit ici de façon raisonnable, à corriger si un jour désassemblé
    // précisément. Retourne false si `name` est absent du roster (no-op).
    bool RemoveMember(const std::string& name) {
        for (int i = 0; i < kMaxSlots; ++i) {
            if (memberNames[static_cast<size_t>(i)] != name) continue;
            if (i == 0) { Reset(); return true; }
            for (int j = i; j < kMaxSlots - 1; ++j)
                memberNames[static_cast<size_t>(j)] = memberNames[static_cast<size_t>(j + 1)];
            memberNames[static_cast<size_t>(kMaxSlots - 1)].clear();
            return true;
        }
        return false;
    }

    // Reset intégral (5 noms + guildName) — Net_OnGuildRosterReset (0x4a) et
    // Net_OnGuildRosterUpdate (0x4f) cas 1/3, cf. §3.
    void Reset() {
        for (auto& n : memberNames) n.clear();
        guildName.clear();
    }
};

// État global du monde de jeu (équivalent des tableaux/globals d'origine).
struct GameWorld {
    SelfState                  self;
    std::vector<PlayerEntity>  players;
    std::vector<MonsterEntity> monsters;
    std::vector<NpcEntity>     npcs;
    // g_NpcRenderArray dword_1764D14 — pool PNJ de rendu/ciblage, 100 slots FIXES (cf.
    // NpcRenderEntry + kNpcRenderPoolCapacity). Dimensionné par GameData_InitPools
    // (== cGameData_InitPools 0x5575D0, propriétaire UNIQUE de la capacité) ; peuplé par
    // Game/StaticNpcLoader.cpp::LoadZoneNpcs (== cGameData_LoadZoneNpcInfo 0x5578E0, écrivain
    // UNIQUE), lui-même déclenché depuis EntityManager::OnSpawnCharacter branche self
    // (== garde `if (!i)` de Pkt_SpawnCharacter @0x4648E6). Ex-`groundItems` (nom erroné,
    // jamais peuplé) — renommé/fusionné par la vague W7, front « npc-array-unify ».
    // Les slots INACTIFS restent présents (index stable, aligné sur mZONENPCINFO[i]) : tout
    // lecteur DOIT tester `active` avant usage.
    std::vector<NpcRenderEntry> npcRenderEntries;
    // Pool des OBJETS AU SOL (sacs de butin) — dword_17AB534 dans le binaire (stride 152 o,
    // catégorie de clic 6, Scene_RayHitItemModel 0x5418B0). CONCEPT DISTINCT du pool PNJ
    // ci-dessus : la vague W7 « npc-array-unify » a prouvé que g_NpcRenderArray 0x1764D14 ne
    // porte QUE des PNJ, jamais du butin. Ce vrai pool d'objets au sol n'est PAS encore
    // modélisé (structure 152 o non portée) ; il reste VIDE — donc ItemPickupSystem
    // (FindNearestGroundItem 0x539EC0 et voisins) demeure inerte, ce qui est FIDÈLE tant que
    // le pool source n'est pas branché. Type provisoire NpcRenderEntry : seuls active/x/y/z
    // sont lus par les consommateurs actuels, tous nuls sur un pool vide.
    std::vector<NpcRenderEntry> groundItems;
    std::vector<ZoneObjectEntity> zoneObjects; // dword_180EEF4, cf. commentaire du struct
    GameDatabases              db;
    PartyRoster                partyRoster;    // g_PartyRosterNames (10 noms), cf. commentaire PartyRoster ci-dessus
    AllianceRoster             allianceRoster; // g_AllianceRosterNames + g_LocalGuildName, cf. commentaire AllianceRoster ci-dessus
    int   zoneId       = 0;
    float gameTimeSec  = 0.0f;   // g_GameTimeSec

    // Pkt_EnterWorld (op 0x0c) VIENT D'ARRIVER : dans le binaire, ce paquet écrit
    // DIRECTEMENT g_SceneMgr.sceneId=6/subState=0 (InGame) — cf. Docs/TS2_PROTOCOL_SPEC.md
    // section "0x0c Pkt_EnterWorld", ligne « écrit : ... dword_1676180=6 ». La bascule de
    // scène n'est PAS une sortie normale de la machine d'état EnterWorldFlow (celle-ci ne
    // détecte qu'un TIMEOUT de secours, cf. Game/EnterWorldFlow.h). EntityManager::
    // OnEnterWorld() (Game/EntityManager.cpp) arme ce flag — il n'a lui-même aucun accès à
    // ts2::SceneManager (couplage réseau -> scène volontairement évité à ce niveau, cf. sa
    // note "hors du perimetre entites"). C'est SceneManager::Update() (case Scene::
    // EnterWorld) qui doit le consommer (test + reset + Change(Scene::InGame)) À CHAQUE
    // frame, EN PRIORITÉ sur EnterWorldFlow_Update — cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md
    // pour le câblage exact côté SceneManager.cpp (fichier en lecture seule pour cette
    // mission, câblage à appliquer manuellement).
    bool  sceneEnterWorldPending = false;

    // Demande de RETOUR en Scene::EnterWorld pour RECHARGER la zone (warp / op 0x18
    // Pkt_GameServerConnectResult 0x469CF0) : sens InGame -> EnterWorld, miroir inverse de
    // sceneEnterWorldPending. Consomme par SceneManager::Update case Scene::EnterWorld
    // (ReloadZone re-entrant, Passe 3 W2-F1). g_TargetZoneId 0x1675A9C / Scene_EnterWorldUpdate 0x52BFF0.
    // Champs ajoutes par MAIN (etat partage de la vague W2) — GameState.h reste en lecture
    // seule pour tous les fronts W2 (aucune double ecriture).
    bool  sceneReloadPending = false;
    int   pendingWarpZoneId  = -1;   // zone cible du rechargement (g_TargetZoneId 0x1675A9C), -1 = aucune

    // Recherche/allocation d'un slot d'entité par identité réseau (comportement
    // des handlers : scan linéaire, sinon 1er slot libre).
    PlayerEntity*  FindOrAddPlayer(EntityId id);
    MonsterEntity* FindOrAddMonster(EntityId id);
    NpcEntity*     FindOrAddNpc(EntityId id);
    PlayerEntity&  Self() { return players.empty() ? (players.emplace_back(), players[0]) : players[0]; }
};

// Instance globale unique.
inline GameWorld g_World;

} // namespace ts2::game

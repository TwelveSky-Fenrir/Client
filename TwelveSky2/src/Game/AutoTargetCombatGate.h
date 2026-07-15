// Game/AutoTargetCombatGate.h — SYSTEME AUTO-CIBLE / GATE DE COMBAT : réécriture C++
// fidèle de deux blocs décompilés via idaTs2 (Hex-Rays). Module Game/*.h/.cpp AUTONOME
// (mission dédiée, 2026-07-14) : n'édite PAS Scene/SceneManager.*, App/App.*, ni aucun
// autre fichier « DÉJÀ ÉCRIT » — l'agent de consolidation câble les fonctions ci-dessous
// sur InGameTickFlowHost::ValidateAutoTarget / ::IsCombatAllowedOnMap (Game/InGameTickFlow.h,
// déjà déclarés là-bas comme hooks opaques std::function, non branchés côté SceneManager.cpp
// à ce jour — cf. son commentaire EA 0x52cca7/0x52cf8e).
//
// Vérité = le désassemblage de TwelveSky2.exe (imagebase 0x400000), décompilé directement
// via MCP idaTs2 pour cette mission (Docs/TS2_GROUNDFX_AUTOTARGET.md et
// Docs/TS2_COMBAT_ELEMENT_GATING.md n'existaient PAS encore au moment de ce travail —
// décompilation directe effectuée à la place, cf. détail des EA ci-dessous).
//
// ---------------------------------------------------------------------------------------
// 1) ValidateAutoTarget — bloc EA 0x52CCA7..0x52CE77 de Scene_InGameUpdate 0x52C600
//    (switch(dword_1675B24), appelé UNIQUEMENT quand la porte de gating de l'étape 12 du
//    tick InGame laisse passer — cf. Game/InGameTickFlow.h, hors périmètre ici).
//    + Char_IsTargetablePlayerState 0x558AE0, Char_IsTargetableMonsterState 0x558B10
//      (prédicats triviaux, décompilés directement : return a1 != 12 / a1 != 12 && a1 != 19).
//
// 2) IsCombatAllowedOnMap — NOM DE MISSION donné à Combat_IsElementAllowedOnMap 0x55CBF0.
//    IMPORTANT (écart entre l'intitulé de mission et le binaire réel, relevé par
//    décompilation directe) : malgré son nom IDA « ...OnMap », cette fonction ne consulte
//    AUCUN identifiant de carte/zone et n'implémente PAS une notion de « zone PVP/safe » —
//    c'est une matrice de compatibilité ÉLÉMENT COURANT (mapElement=g_LocalElement) x PAIRE
//    D'ALLIANCE (Char_GetPairedElement) x MORPH ACTIF (g_SelfMorphNpcId), qui gate en réalité
//    le ramassage automatique des marqueurs de combo élémentaire (flt_1676130, 5
//    emplacements) — PAS le combat PVP. Aucune table « carte -> PVP/safe » n'existe dans le
//    binaire à ce site d'appel (seul appelant confirmé : EA 0x52cf7a, cf. xrefs_to). Cette
//    fonction est DÉJÀ intégralement portée par Game/ComboPickupTick.h/.cpp
//    (Combat_IsElementAllowedOnMap, même EA, même logique confirmée par décompilation
//    indépendante ci-dessous) : ce fichier ne la RÉIMPLÉMENTE PAS, il l'expose sous le nom
//    demandé par la mission (wrapper direct, cf. IsCombatAllowedOnMap ci-dessous) pour
//    satisfaire le nom du hook InGameTickFlowHost::IsCombatAllowedOnMap.
//
// ---------------------------------------------------------------------------------------
// AMBIGUÏTÉ SIGNALÉE (même politique que GroupIdentity dans Game/GameState.h) :
// dword_1675B24 (adresse du « mode » de cible auto ci-dessous) est RÉUTILISÉ tel quel par
// Net/GameHandlers_VendorTrade.cpp / UI/PlayerTradeWindow.h comme « état d'échange entre
// joueurs » — MÊME adresse mémoire, sémantique DIFFÉRENTE. Aucun conflit réel côté binaire :
// les deux systèmes ne s'exécutent jamais simultanément (fenêtre d'échange modale piloté par
// les paquets 0x31/0x33 vs ciblage auto du tick InGame). Reproduit ici via le MÊME
// échappatoire game::g_Client.Var (Game/ClientRuntime.h) déjà utilisé pour CES TROIS
// adresses précises par Game/AutoPlaySystem.cpp (cf. UpdateTargeting, EA 0x45D080) — AUCUNE
// duplication de stockage introduite par ce fichier.
//
// RÉUTILISATION (règle de la mission — ne PAS dupliquer un système déjà écrit) :
//   - Combat_IsElementAllowedOnMap + ElementPairTable sont déjà portés par
//     Game/SkillCombat.h / Game/ComboPickupTick.h — RÉUTILISÉS tels quels (wrapper, cf. ci-
//     dessus), aucune réimplémentation de la matrice élément/morph ici.
//   - g_Client.Var/VarF (Game/ClientRuntime.h) — échappatoire globals déjà standard dans
//     tout ClientSource, réutilisée pour dword_1675B24/28/2C ET g_SelfMorphNpcId
//     (0x1675A98, MÊME convention que Net/GameVarDispatch.cpp / Net/WorldEntityDispatch.cpp
//     / Game/AnimationTick.cpp — aucune instance CombatMorphState globale n'existe à ce
//     jour dans ClientSource, lu en lecture seule ici comme partout ailleurs).
//   - g_World.self.element (SelfState::element, Game/GameState.h, MÊME adresse
//     g_LocalElement 0x1673194) — même convention de lecture que
//     Net/GameHandlers_InvDispatch.cpp ligne ~214.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/ComboPickupTick.h" // Combat_IsElementAllowedOnMap + Game/SkillCombat.h::ElementPairTable

namespace ts2::game {

// ===========================================================================
// 1) ValidateAutoTarget — état/adresses d'origine.
// ===========================================================================

// Adresses d'origine des 3 globales pilotant la cible auto verrouillée.
inline constexpr uint32_t kAutoTargetModeAddr   = 0x1675B24u; // dword_1675B24 : mode (0/1/2/3/4/5/7)
inline constexpr uint32_t kAutoTargetIdHiAddr   = 0x1675B28u; // dword_1675B28 : id.hi (modes 1/2/3/5) OU index brut (modes 4/7)
inline constexpr uint32_t kAutoTargetIdLoAddr   = 0x1675B2Cu; // dword_1675B2C : id.lo (modes 1/2/3/5 uniquement)
inline constexpr float    kAutoTargetRangeLimit = 500.0f;     // EA 0x52cd91/0x52ce6b (invalide si distance > 500.0, strict)

// Char_IsTargetablePlayerState 0x558AE0 — EA confirmée par décompilation directe :
//   BOOL __stdcall Char_IsTargetablePlayerState(int a1) { return a1 != 12; }
// (TODO non résolu ailleurs à ce jour dans ClientSource — cf. Game/ActionStateMachine.cpp,
// commentaire EA 0x571B8F qui l'approxime encore par « active && id correspond » ; ce
// fichier fournit l'implémentation exacte pour SON propre usage, sans éditer ce fichier-là).
inline bool Combat_IsTargetablePlayerState(int actionState) { return actionState != 12; }

// Char_IsTargetableMonsterState 0x558B10 — idem : return a1 != 12 && a1 != 19.
inline bool Combat_IsTargetableMonsterState(int actionState) {
    return actionState != 12 && actionState != 19;
}

// dword_168724C[227*kk] — lecture du 1er dword du payload spawn joueur, = PlayerEntity::
// body[0..3]. Adresse EXACTE confirmée par arithmétique (dword_1687234=active@+0 ->
// +4/+8=id -> +0xC=timestamp -> +0x18(24)=body[0], et dword_168724C-dword_1687234=0x18) :
// dword_168724C EST body[0..3] réinterprété en int32. Sémantique fine non déterminée
// (probablement id de classe/apparence, même famille que NpcEntity/MonsterEntity::body[0]
// = mob id) ; testé pour simple non-nullité (« enregistrement peuplé »), fidèle au binaire
// qui ne teste QUE la vérité/fausseté de ce dword (aucune comparaison de valeur).
inline bool AutoTarget_PlayerRecordPopulated(const PlayerEntity& p) {
    int32_t v = 0;
    std::memcpy(&v, p.body.data(), sizeof(v));
    return v != 0;
}

// dword_1766F8C[70*mm] — état d'action « brut » du monstre LU DIRECTEMENT depuis le corps,
// PAS MonsterEntity::anim.state. Adresse EXACTE confirmée par arithmétique (dword_1766F74=
// active@+0 -> +4/+8=id -> +0xC=timestamp -> +0x10(16)=body début, cohérent avec
// GameState.h "def +0x60" = 16+80 -> dword_1766F8C-dword_1766F74=0x18(24) = body[24-16=8..11]).
// Ce champ N'EST PAS garanti identique à MonsterEntity::anim.state (extrait par une AUTRE
// fonction, Char_Update 0x581E10, offsets propres non confirmés alignés sur celui-ci) : lu
// ici depuis le corps brut pour rester fidèle à CETTE lecture précise du binaire, sans
// dépendre d'une hypothèse non prouvée sur anim.state.
inline int32_t AutoTarget_MonsterActionState(const MonsterEntity& m) {
    int32_t v = 0;
    std::memcpy(&v, m.body.data() + 8, sizeof(v));
    return v;
}

// Oracle de position pour les modes « objet à portée » (4 = MÊME pool 88o exposé côté
// ClientSource sous g_World.groundItems (dword_1764D14/g_NpcRenderArray) ; 7 =
// ZoneObjectEntity/g_World.zoneObjects). `mode` reçoit la valeur brute de dword_1675B24 (4 ou
// 7) pour que l'appelant distingue les deux pools. Renvoie faux -> cible considérée hors de
// portée (repli sûr, MÊMES conséquences qu'un pool vide/index invalide côté binaire).
//
// *** CORRECTIF SÉMANTIQUE (mission « AutoTargetRangeLookup non câblé », 2026-07-14,
// décompilation fraîche indépendante de Item_PickupTarget 0x539EC0 ET
// World_PickEntityAtCursor 0x538AB0) *** : le paragraphe ci-dessous (et celui de
// AutoTarget_DefaultRangeLookup) affirmait que dword_1764D14/g_NpcRenderArray était « LE
// MÊME tableau g_World.groundItems » au sens gameplay (« objets au sol »). C'est CONFIRMÉ
// FAUX à l'usage : World_PickEntityAtCursor traite CE MÊME tableau (boucle `j`, stride 22 dw,
// borne g_NpcCount) comme catégorie de clic **4 = NPC**, via `Scene_RayHitNpcBox` (nom IDA
// explicite, PAS un raycast d'objet) ; et Item_PickupTarget 0x539EC0 (qui LIT CE MÊME
// tableau, MÊME offset unk_1764D28, pour la garde de portée du mode 4 ci-dessous) n'effectue
// JAMAIS de ramassage : son seul effet observable est `UI_NpcWin_Open(&g_NpcRenderArray[...])`
// — une fenêtre de DIALOGUE NPC. Le VRAI pool "objet ramassable au sol" est un AUTRE tableau,
// dword_17AB534 (stride 38 dw/152 o, catégorie de clic **6** dans World_PickEntityAtCursor,
// raycast `Scene_RayHitItemModel`) — celui-là même que GameState.h modélise (à l'envers,
// même confusion) sous le nom `NpcEntity`/`g_World.npcs`. CONCLUSION (même diagnostic déjà
// posé indépendamment par Game/GroundAuraWorldObjectTick.h, § « MISE À JOUR audit étapes
// 5-8 » — confirmé ici par une 4e trace croisée) : `g_World.groundItems` est un NOM DE CHAMP
// erroné hérité de GameState.h (fichier socle partagé, HORS PÉRIMÈTRE d'édition de cette
// mission) — le pool qu'il porte réellement est un tableau de NPCs, pas d'objets au sol. CE
// QUI NE CHANGE RIEN AU CÂBLAGE CI-DESSOUS : le mode 4 de g_PendingOrderKind (walk-to/target
// NPC, cohérent avec la séquence 1-3=joueur, 4=npc, 5=monstre, 7=objet de zone dans
// World_PickEntityAtCursor) a TOUJOURS besoin de lire CE tableau précis (adresse+stride+
// offset), et `g_World.groundItems` EST structurellement ce tableau (même adresse dérivée,
// même stride 88, mêmes offsets x/y/z) — c'est donc le SEUL conteneur ClientSource correct à
// utiliser ici, malgré son nom. Aucun autre pool ne convient : `g_World.npcs` (dword_17AB534)
// a un stride/offset totalement différent (152 o) et correspond en réalité au tableau
// d'objets-modèles (catégorie 6), PAS à celui lu par ce switch. Renommer le champ
// GameState.h::groundItems -> npcRenderEntries (et vice-versa pour npcs) est une correction
// de fond qui déborde du périmètre Game/AutoTargetCombatGate.*.h de cette mission (fichier
// socle partagé) — signalé ici pour une future passe de cohérence, PAS appliqué.
using AutoTargetRangeLookup = std::function<bool(int mode, int index, float outPos[3])>;

// Oracle par défaut : mode==4 -> x/y/z lus depuis g_World.groundItems (storage RÉUTILISÉ tel
// quel — cf. correctif sémantique ci-dessus : ce pool est en réalité le tableau NPC
// g_NpcRenderArray/dword_1764D14, PAS des objets ramassables, mais c'est bien le tableau EXACT
// que ce mode doit lire) ; mode==7 -> x/y/z = 3 premiers floats de ZoneObjectEntity::body.
//
// mode==4, EA 0x52cd91 : `Math_Dist3D((char*)&unk_1764D28 + 88*g_PendingOrderGridX,
// flt_1687330)`. unk_1764D28 - g_NpcRenderArray(dword_1764D14) == 0x14 (20 o), stride 88 o,
// index brut = g_PendingOrderGridX -- adresse/stride/offset RE-CONFIRMÉS par 2 décompilations
// indépendantes supplémentaires cette mission (Item_PickupTarget 0x539EC0, MÊME expression
// `unk_1764D28 + 22*index` ; World_PickEntityAtCursor 0x538AB0, boucle `j` MÊME tableau/
// stride/borne g_NpcCount). AUCUN test d'activité (`active`) dans le binaire pour ce mode
// (contrairement à ce qu'on pourrait attendre) — reproduit fidèlement : seul un bounds-check
// défensif est ajouté ci-dessous (nécessaire car g_World.groundItems est un std::vector à
// croissance paresseuse côté ClientSource, PAS un tableau C de capacité fixe garantie comme
// l'original).
//
// mode==7, EA 0x52ce6b : fidélité confirmée sur l'adresse (unk_180EF0C - g_ZoneObjectArray ==
// 0x18 == ZoneObjectEntity::body offset 0, cf. GameState.h) ET RE-CONFIRMÉE par
// World_PickEntityAtCursor (boucle `n`, catégorie 7 = g_ZoneObjectArray, MÊME tableau). Le
// binaire NE TESTE PAS non plus z.active pour ce mode -- SEUL le bounds-check défensif (même
// raison que mode 4) est appliqué ici, sans filtrer sur l'activité du slot.
bool AutoTarget_DefaultRangeLookup(const GameWorld& world, int mode, int index, float outPos[3]);

// ValidateAutoTarget — bloc EA 0x52CCA7..0x52CE77 de Scene_InGameUpdate (switch sur
// dword_1675B24). Valide la cible actuellement verrouillée et RAZ le mode
// (g_Client.Var(kAutoTargetModeAddr)=0) si elle n'est plus valide — sinon ne touche à RIEN
// (fidèle : le binaire ne modifie jamais dword_1675B28/2C ici, seul le mode peut retomber
// à 0).
//   mode 1/2/3 : cible = joueur distant (world.players[1..], l'index 0 = self est ignoré,
//                fidèle à la boucle `for (kk=1; kk<g_EntityCount; ...)`) — retrouvé par
//                (active && AutoTarget_PlayerRecordPopulated && Combat_IsTargetablePlayerState
//                (anim.state) && id.hi==dword_1675B28 && id.lo==dword_1675B2C). RAZ si AUCUN
//                joueur ne correspond (fidèle : recherche exhaustive, PAS de test de
//                distance pour ces 3 modes).
//   mode 5     : cible = monstre (world.monsters[0..]) — même patron, via
//                Combat_IsTargetableMonsterState(AutoTarget_MonsterActionState(m)).
//   mode 4     : cible = « NPC à portée » (PAS un objet au sol malgré le nom du champ
//                g_World.groundItems qui porte ce pool côté ClientSource -- cf. correctif
//                sémantique dans le bandeau de AutoTarget_DefaultRangeLookup) — index BRUT =
//                dword_1675B28 (PAS un id réseau, fidèle : le binaire indexe directement le
//                pool g_NpcRenderArray, aucun scan d'existence, ET AUCUN test d'activité du
//                slot -- RE-VÉRIFIÉ, cf. AutoTarget_DefaultRangeLookup).
//   mode 7     : cible = « objet de zone à portée » (ZoneObjectEntity/g_World.zoneObjects) —
//                même patron d'indexation brute que le mode 4.
//                RAZ (modes 4 et 7) si `rangedLookup` renvoie faux OU distance > 500.0 de self.
//   défaut     : aucune action (fidèle : le binaire ignore silencieusement tout autre mode,
//                y compris 0 déjà nul et toute valeur jamais observée dans ce switch, ex 6 —
//                catégorie "objet ramassable"/dword_17AB534 dans World_PickEntityAtCursor,
//                absente de CE switch précis).
// `rangedLookup` par défaut (nul) => AutoTarget_DefaultRangeLookup(world, ...) (modes 4 ET 7,
// cf. bandeau ci-dessus -- mode 4 résolu via world.groundItems, storage réutilisé tel quel).
void ValidateAutoTarget(GameWorld& world,
                         const AutoTargetRangeLookup& rangedLookup = nullptr);

// ===========================================================================
// 2) IsCombatAllowedOnMap — wrapper EXACT de Combat_IsElementAllowedOnMap (déjà porté).
// ===========================================================================

// Wrapper direct de Game::Combat_IsElementAllowedOnMap (Game/ComboPickupTick.h, EA
// 0x55CBF0) — RÉUTILISÉ tel quel, AUCUNE réimplémentation de la matrice élément/morph.
// Exposé sous le nom demandé par la mission ET par le hook
// InGameTickFlowHost::IsCombatAllowedOnMap (Game/InGameTickFlow.h). `pairs` = table de
// paires d'éléments du personnage (g_LocalPlayerSheet+455..458) — AUCUNE instance globale
// n'existe dans ClientSource à ce jour (cf. Scene/SceneManager.cpp, commentaire EA
// 0x52cf8e) ; {} (4x -1, « aucune paire enregistrée ») est le repli par défaut le plus
// proche d'un personnage n'ayant jamais reçu cette donnée réseau — PAS un raccourci
// « toujours faux » : le résultat dépend TOUJOURS réellement de `selfMorphNpcId` (cf.
// Combat_IsElementAllowedOnMap, branche -1/« soi » de chaque switch interne, EA 0x55cc38
// et suivants).
inline bool IsCombatAllowedOnMap(int mapElement, int selfMorphNpcId,
                                  const ElementPairTable& pairs = {}) {
    return Combat_IsElementAllowedOnMap(mapElement, selfMorphNpcId, pairs);
}

// Variante « zéro-argument » directement branchable sur
// InGameTickFlowHost::IsCombatAllowedOnMap (std::function<bool()>, EA 0x52cf6e-0x52cf94) :
//   mov ecx, g_LocalPlayerSheet ; push g_LocalElement ; call Combat_IsElementAllowedOnMap
// mapElement = world.self.element (g_LocalElement 0x1673194, même convention de lecture que
// Net/GameHandlers_InvDispatch.cpp) ; selfMorphNpcId = g_Client.VarGet(0x1675A98)
// (échappatoire g_SelfMorphNpcId, même convention que Net/GameVarDispatch.cpp /
// Net/WorldEntityDispatch.cpp / Game/AnimationTick.cpp). NOTE : le binaire combine ce
// résultat avec `&& !g_GmAuthLevel` AU SITE D'APPEL (0x52cf8e) — Game/InGameTickFlow.cpp
// applique déjà cette combinaison via un hook IsGm SÉPARÉ (étape 12d) ; cette fonction ne
// renvoie QUE le résultat de Combat_IsElementAllowedOnMap, fidèle au découpage établi.
bool IsCombatAllowedOnMapForSelf(const GameWorld& world, const ElementPairTable& pairs = {});

} // namespace ts2::game

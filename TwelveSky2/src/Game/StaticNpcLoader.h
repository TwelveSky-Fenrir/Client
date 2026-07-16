// Game/StaticNpcLoader.h — chargeur de PNJ statiques de DECOR (marchands, gardes,
// donneurs de quete...) place par la carte, EQUIVALENT client-source de
// `cGameData_LoadZoneNpcInfo` 0x5578E0 (renomme dans l'IDB, ex-`cGameData_LoadMapEffects`).
//
// Contexte complet, preuve et decompilation : Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md.
// Resume :
//   - Le client d'origine peuple un tableau `g_NpcRenderArray` (0x1764D14, stride 88 o,
//     100 slots) DEPUIS UNE TABLE STATIQUE PAR-ZONE `mZONENPCINFO` chargee UNE FOIS au
//     demarrage (G02_GINFO\002.BIN, deja cable cote ClientSource, cf. MotionPools.h
//     §3 "mZONENPCINFO" + ZoneNpcCount/ZoneNpcKindId/ZoneNpcPosition/ZoneNpcAngle).
//   - Ce tableau est REPEUPLE INTEGRALEMENT (jamais fusionne/patche) a CHAQUE fois que
//     le paquet `Pkt_SpawnCharacter` (opcode 0x0F) cree l'entree i==0 (= joueur local)
//     du tableau d'entites — c'est-a-dire a CHAQUE cycle EnterWorld+SpawnCharacter(self),
//     donc au login initial ET a chaque (re)chargement de zone/warp/teleport (le serveur
//     renvoie systematiquement un EnterWorld+SpawnCharacter(self) a chaque changement de
//     zone, cf. Docs/TS2_PROTOCOL_SPEC.md). Preuve : decompilation integrale de
//     Pkt_SpawnCharacter 0x4646C0 (cf. doc), garde `if (!i)` juste apres la creation du
//     slot, AVANT tout traitement specifique aux entites deja existantes.
//   - C'est un mecanisme ENTIEREMENT LOCAL (aucun paquet reseau dedie aux PNJ de decor :
//     l'unique opcode PNJ reseau, 0x13 Pkt_SpawnNpc, alimente un tableau GAMEPLAY
//     disjoint `dword_17AB534`/`game::NpcEntity`/`g_World.npcs`, utilise UNIQUEMENT pour
//     l'interaction/ciblage — jamais pour le rendu du maillage, cf. Docs/
//     TS2_NPC_MESH_DRAW.md §"tableaux gameplay vs rendu"). Les deux tableaux se
//     recouvrent conceptuellement (memes PNJ) mais ne sont PAS synchronises entre eux
//     cote binaire d'origine : le rendu du maillage PNJ (Npc_DrawMesh 0x57FF00) ne lit
//     QUE `g_NpcRenderArray`, jamais `dword_17AB534`.
//
// Declenchement cable : Game/EntityManager.cpp::OnSpawnCharacter, branche "nouveau slot",
// quand `IsSelf(e)` est vrai (equivalent exact du garde `if (!i)` d'origine, @0x4648E6) —
// appelle `LoadZoneNpcs(g_World.zoneId)`. Ce chemin est REELLEMENT ATTEINT au runtime
// (verifie W7 : EntityManager.cpp:318), c'est la garantie anti-code-mort de ce module.
//
// VERIFIE 2026-07-14 (mission dediee "fidelite positions PNJ decor") : le mapping
// zoneId1Based -> ligne mZONENPCINFO (row = zoneId1Based - 1, MotionPools::AttachTableRow)
// est DIRECT, SANS TABLE DE CORRESPONDANCE INTERMEDIAIRE cote binaire d'origine — confirme
// par decompilation de cGameData_LoadZoneNpcInfo 0x5578E0 (`mZONENPCINFO[501 *
// g_SelfMorphNpcId - 501]`, formule affine pure). Le seul point subtil est la SOURCE du
// zoneId d'origine : le binaire reutilise deliberement le global g_SelfMorphNpcId (0x1675A98,
// "Id de forme courante"/morph) comme porteur temporaire du zoneId cible, reprime a CHAQUE
// passage par Scene_EnterWorldUpdate (login ET tout warp/teleport/respawn — 41 sites
// d'ecriture releves sur dword_1675A9C, cf. Docs/TS2_NPC_RENDER_ARRAY_WRITER.md §7 pour la
// preuve desassemblage complete). Ceci confirme et resout un point precedemment ouvert (§7,
// "hypothese 2" retenue) et valide que la formule d'indexation deja implementee ici
// (zoneId1Based - 1, aucun LUT) est fidele a l'original.
//
// ============================================================================================
// MISE A JOUR Passe 4 / vague W7 — front "npc-array-unify" (fusion des DEUX representations)
// ============================================================================================
// AVANT W7, ce module portait son PROPRE `std::vector<StaticNpcSlot> g_zoneNpcs` (statique de
// fichier), tandis que Game/GameState.h portait un SECOND modele du MEME tableau d'origine
// (`GameWorld::groundItems`, nom errone, JAMAIS peuple). Resultat : deux representations du
// pool g_NpcRenderArray 0x1764D14 dont les consommateurs s'ignoraient — rendu/minimap lisaient
// celle-ci, clic/tick/ciblage lisaient l'autre (donc du code MORT).
//
// Desormais : representation UNIQUE = `GameWorld::npcRenderEntries` (Game/GameState.h, struct
// NpcRenderEntry — layout PROUVE, 100 slots FIXES). Ce module en est l'ECRIVAIN (equivalent de
// cGameData_LoadZoneNpcInfo 0x5578E0, ecrivain unique cote binaire) ; `ZoneNpcs()` n'est plus
// qu'un ACCESSEUR mince sur ce pool, conserve pour ne pas casser ses lecteurs existants.
//
// DEUX ECARTS DE FIDELITE CORRIGES PAR W7 (l'ancien vecteur prive les portait) :
//  1. PERTE D'INDEX (visible sur le RESEAU). L'ancien code faisait `push_back` -> il COMPACTAIT
//     la liste : un kindId sans record decalait tous les index suivants. Le binaire, lui, garde
//     le slot `i` aligne sur `mZONENPCINFO[i]` (il laisse un TROU inactif). Or cet index part
//     sur le reseau : `Net_QueueRunTo(..., 4, a1, ...)` @0x539E78, ou `a1` est l'index de slot
//     resolu par World_PickEntityAtCursor (`*a4 = j` @0x538E8F). Compacter = designer une AUTRE
//     cible au serveur. Le pool a 100 slots fixes corrige donc un ECART RESEAU, pas seulement
//     un doublon de modelisation.
//  2. CLEAR PARASITE. Le binaire n'efface RIEN dans ce chargeur (aucun `else` sur la garde
//     @0x557956, aucune remise a zero des slots >= count) : le nettoyage appartient
//     EXCLUSIVEMENT a Pkt_EnterWorld (boucle `for i<g_NpcCount: dtor(slot)` @0x464237, dtor
//     0x57FE70 qui ne remet QUE +4=0). L'ancien `g_zoneNpcs.clear()` etait donc infidele.
// ============================================================================================
#pragma once
#include "Game/ExtraDatabases.h"
#include "Game/GameState.h" // NpcRenderEntry / GameWorld::npcRenderEntries / kNpcRenderPoolCapacity
#include <cstdint>
#include <vector>

namespace ts2::game {

// Alias CONSERVE du nom historique de ce module : le "slot de PNJ statique" EST une entree du
// pool unique g_NpcRenderArray (Game/GameState.h::NpcRenderEntry — layout prouve : def(+0),
// active(+4), mode(+12), frameAcc(+16), x/y/z(+20/24/28), angle(+44), angleBase(+80)).
// Maintenu pour que les lecteurs existants (Scene/WorldRenderer.cpp, UI/MinimapWidget.cpp)
// continuent de compiler sans changement de type.
//
// NOTE : le champ `kindId` de l'ancien StaticNpcSlot a DISPARU — il n'existe PAS dans les 88 o
// du binaire (le chargeur consomme le kindId a la volee pour resoudre `def` et ne le stocke
// jamais ; les lecteurs relisent le kind via `def` : Npc_RenderSlotTick_Loop @0x580429 lit
// def+1324, UI_NpcWin_Open 0x5DB530 @0x5dc03a lit `*(*a2)` == def+0 == NpcDefRecord::id).
// CORRECTION W7 (re-verifie par Grep) : contrairement a la 1re redaction, UN lecteur C++ le
// reference ENCORE — Game/AnimationTick.cpp:923 (ZoneNpc_OnDialogueOpen, portage de 0x5DB530)
// fait `slot.def ? slot.def->id : slot.kindId` en repli. Ce repli est MORT cote binaire
// (0x5DB530 lit def+0 sur un slot ACTIF, donc def non-nul) mais casse la COMPILATION depuis la
// fusion (NpcRenderEntry n'a pas de membre kindId). kindId reste HORS layout (fidelite aux 88 o
// prouvee) : c'est AnimationTick.cpp:923 qui doit etre adapte par l'orchestrateur (`: slot.kindId`
// -> `: 0`, fichier hors perimetre de ce front). Seule occurrence de membre `.kindId` dans src/.
using StaticNpcSlot = NpcRenderEntry;

// Repeuple le pool g_NpcRenderArray pour `zoneId1Based` (1..350) — equivalent client-source de
// cGameData_LoadZoneNpcInfo 0x5578E0. Necessite que MotionPools::LoadGInfo002Bin() ET
// ExtraDatabases::LoadExtraDatabases() aient deja ete appeles (App_Init), ET que
// GameData_InitPools() ait dimensionne le pool (== cGameData_InitPools 0x5575D0, proprietaire
// UNIQUE de la capacite) ; sinon renvoie false sans rien ecrire.
//
// N'EFFACE RIEN (cf. ecart #2 du bandeau) : ecrit les slots [0, count) EN PLACE, index par
// index. Pour chaque i : `def` = GetNpcDefRecord(ZoneNpcKindId(...)) est ecrit
// INCONDITIONNELLEMENT (@0x557946) ; si `def == nullptr`, la garde @0x557956 (sans `else`)
// laisse `active` TEL QUEL et le slot i reste un TROU — l'index i n'est jamais reattribue
// (cf. ecart #1). Les slots >= count conservent leur etat : la sequence d'origine est
// EnterWorld (desactive les 100 slots @0x464237) -> SpawnCharacter(self) -> ce chargeur.
bool LoadZoneNpcs(int zoneId1Based);

// Accesseur (mince) sur le pool unique `g_World.npcRenderEntries`.
//
// /!\ CONTRAT CHANGE PAR W7 : renvoie desormais les 100 SLOTS FIXES du pool, et NON une liste
// compactee de slots occupes. Tout lecteur DOIT tester `n.active` avant usage, sous peine de
// traiter des slots vides (def == nullptr, position 0,0,0).
const std::vector<NpcRenderEntry>& ZoneNpcs();

// zoneId1Based utilise par le dernier LoadZoneNpcs() reussi (0 si aucun).
int CurrentZoneNpcZoneId();

} // namespace ts2::game

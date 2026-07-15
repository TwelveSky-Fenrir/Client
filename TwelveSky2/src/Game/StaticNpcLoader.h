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
//   - Ce tableau est REPEUPLE INTEGRALEMENT (jamais fusionne/patché) a CHAQUE fois que
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
// Ce module fournit l'EQUIVALENT DE DONNEES de `g_NpcRenderArray` (le "quoi" : quels PNJ,
// ou, avec quel modele) — PAS le rendu lui-meme (le "comment dessiner", qui appartient a
// Scene/WorldRenderer.cpp, HORS PERIMETRE de ce module par consigne de mission).
//
// Declenchement cable : Game/EntityManager.cpp::OnSpawnCharacter, branche "nouveau slot",
// quand `IsSelf(e)` est vrai (equivalent exact du garde `if (!i)` d'origine) — appelle
// `LoadZoneNpcs(g_World.zoneId)`. LIMITE CONNUE (documentee, pas une invention silencieuse) :
// `g_World.zoneId` n'est aujourd'hui ecrit qu'UNE FOIS, a la reponse serveur de selection de
// personnage (UI/LoginScene.cpp, cf. Net/CharSelectPackets.h) — rien dans ClientSource ne le
// remet a jour sur un warp/teleport en cours de partie (Pkt_ZoneChangeInfo, opcode 0x13
// d'origine cote reseau, n'est pas encore decode/branche sur GameState). Ce module suit donc
// fidelement `g_World.zoneId` : il sera correct au login initial et se re-declenchera
// correctement des que ce gap sera comble par ailleurs (mise a jour de g_World.zoneId AVANT
// le prochain OnEnterWorld/OnSpawnCharacter(self)) — aucune hypothese supplementaire n'est
// prise ici.
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
#pragma once
#include "Game/ExtraDatabases.h"
#include <cstdint>
#include <vector>

namespace ts2::game {

// Un slot peuple, EQUIVALENT d'une entree occupee de g_NpcRenderArray. Ne modelise QUE les
// champs "donnees" ecrits par cGameData_LoadZoneNpcInfo (ptr/pos/angle) — PAS les champs
// d'etat de rendu par-frame (+12 mode tick / +16 accumulateur, proprietes de
// Npc_RenderSlotTick_Loop/_Once 0x580400/0x5804A0, hors perimetre "chargement", proprete du
// futur code de rendu s'il les reimplemente).
struct StaticNpcSlot {
    uint32_t kindId = 0;              // 1-based, index dans mNPC (ExtraDatabases.h)
    const NpcDefRecord* def = nullptr; // GetNpcDefRecord(kindId) — jamais nullptr si le slot
                                        // existe (garde `if (record)` fidele a l'original,
                                        // cf. .cpp : un kindId sans record ne cree PAS de slot)
    float x = 0.0f, y = 0.0f, z = 0.0f; // position monde (mZONENPCINFO+0x194, vec3/entree)
    float angle = 0.0f;                 // angle affiche initial (mZONENPCINFO+0x644),
                                         // == angleBase a la creation (pas de divergence tant
                                         // que le renderer n'a pas encore fait tourner l'un
                                         // sans l'autre — cf. Npc_RenderSlotTick_Loop qui relit
                                         // la baseline au-dela de 400 unites de distance camera,
                                         // logique HORS PERIMETRE "chargement" de ce module)
};

// Recharge INTEGRALEMENT la liste de PNJ statiques pour `zoneId1Based` (1..350) — vide et
// repeuple depuis zero a chaque appel, fidele a cGameData_LoadZoneNpcInfo (jamais de fusion
// incrementale). Necessite que MotionPools::LoadGInfo002Bin() ET ExtraDatabases::
// LoadExtraDatabases() aient deja ete appeles (App_Init) ; sinon renvoie false et laisse la
// liste vide.
//
// Pour chaque i in [0, ZoneNpcCount(zoneId1Based)) : resout kindId=ZoneNpcKindId(...) puis
// record=GetNpcDefRecord(kindId) ; si record==nullptr, le slot est IGNORE (pas cree) --
// fidele au garde `if (*((_DWORD*)this + 22*i + 228723))` de cGameData_LoadZoneNpcInfo, qui
// ne pose flag_occupe=1 QUE si SkillDefTbl_GetRecord a reussi. Le nombre de slots resultant
// peut donc etre < ZoneNpcCount(zoneId1Based) si des kindId sont invalides/hors table.
bool LoadZoneNpcs(int zoneId1Based);

// Liste actuelle (vide si LoadZoneNpcs n'a jamais reussi, ou si la zone n'a aucun PNJ).
const std::vector<StaticNpcSlot>& ZoneNpcs();

// zoneId1Based utilise par le dernier LoadZoneNpcs() reussi (0 si aucun).
int CurrentZoneNpcZoneId();

} // namespace ts2::game
